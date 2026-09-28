# Переносимый AI Agent для ESP-IDF

Компонент `components/ai_agent/` подключает стандартный локальный HTTP API к
существующему `httpd_handle_t`. Он не запускает второй сервер, не настраивает
Wi-Fi, не обращается к GPIO и не зависит от BLE Lock. Проверяемая версия SDK:
ESP-IDF 5.4.4, ESP32-S3.

## Архитектура

До выделения компонента `device-agent.ps1` распознавал только модель замка и
версии `10.0.x`. `main/web_ui.c` одновременно обслуживал Web UI, разбирал команды
Agent, вызывал автомат/ULP, читал журналы и выполнял HTTP upload OTA/restart.

Теперь цепочка выглядит так:

```text
device-agent.ps1
    → ai_agent_http.c: URI, parsing, authorization hook, HTTP responses
    → ai_agent.c: metadata, immutable command registry, status/event providers
    → main/lock_ai_adapter.c: конфигурация и callbacks конкретного устройства
    → существующие lock_request_*, ULP, диагностика, event_log, remote_log

HTTP OTA/restart → ai_agent_ota.c → стандартные esp_ota_* / esp_restart
                                 → прикладные hooks LED и подготовки Wi-Fi
```

В другом transport можно вызывать `ai_agent_execute`, `ai_agent_read_status`,
`ai_agent_read_events`, `ai_agent_info_json`, `ai_agent_capabilities_json` без
`httpd_req_t`. MQTT/WebSocket пока не реализованы. Потоковый upload OTA и
планирование restart сейчас реализованы в HTTP-службе компонента; для другого
transport понадобится соответствующая служба передачи образа и ответа.

## Подключение к новому проекту

1. Скопируйте **весь каталог `components/ai_agent/`** в `components/` проекта.
2. Добавьте `ai_agent` в `REQUIRES` приложения. Все зависимости компонента —
   штатные компоненты IDF: `esp_http_server`, `app_update`, `esp_system`,
   `esp_hw_support`, `freertos`, `json`. Файлы из `main/` BLE Lock не нужны.
3. Приложение подключает Wi-Fi/Ethernet и создаёт либо переиспользует HTTP server.
   Оставьте восемь свободных URI slots и stack не менее 6144 bytes для показанного
   варианта. Callback приложения может потребовать больше стека.
4. Заполните metadata, providers и registry до публикации сервера/другого transport.
5. Вызовите `ai_agent_register_http_handlers(server)`. Регистрация закрывает registry
   для изменений. При ошибке откатываются только URI, добавленные этим вызовом.
   Если сервер остановлен приложением, handlers можно зарегистрировать на новом
   server без повторного `ai_agent_init`.
6. Для OTA нужны `otadata` и как минимум два подходящих app-раздела. Передаётся
   application `.bin`, а не merged flash image, bootloader или partition table.

Минимальная интеграция с уже созданным сервером:

```c
#include <stdatomic.h>
#include <stdio.h>
#include "ai_agent.h"
#include "ai_agent_http.h"

static atomic_bool enabled;

static esp_err_t relay_on(const ai_agent_args_t *args, void *ctx)
{
    (void)args;
    atomic_store((atomic_bool *)ctx, true);
    return ESP_OK;
}

static esp_err_t relay_off(const ai_agent_args_t *args, void *ctx)
{
    (void)args;
    atomic_store((atomic_bool *)ctx, false);
    return ESP_OK;
}

static esp_err_t status(char *out, size_t capacity, void *ctx)
{
    int n = snprintf(out, capacity, "{\"enabled\":%s}",
                     atomic_load((atomic_bool *)ctx) ? "true" : "false");
    return n >= 0 && (size_t)n < capacity ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

void attach_agent(httpd_handle_t server)
{
    ai_agent_config_t cfg = {
        .device_type = "relay_controller",
        .device_name = "Relay Controller",
        .firmware_version = "1.0.0",
        .device_id = "relay-001", // В production — стабильный уникальный ID.
        .history_url = NULL,     // Можно указать URI внешнего архива.
        .status = status,
        .ctx = &enabled,
    };
    ESP_ERROR_CHECK(ai_agent_init(&cfg));
    const ai_agent_command_t on = {"relay_on", "Enable relay", relay_on, &enabled};
    const ai_agent_command_t off = {"relay_off", "Disable relay", relay_off, &enabled};
    ESP_ERROR_CHECK(ai_agent_register_command(&on));
    ESP_ERROR_CHECK(ai_agent_register_command(&off));
    ESP_ERROR_CHECK(ai_agent_register_http_handlers(server));
}
```

Это пример состояния реле в RAM. Для физического реле callback должен передать
запрос существующей задаче управления оборудованием. Полный компилируемый пример
с `app_main`, CMake и OTA partition configuration находится в
`components/ai_agent/example/`. Сетевое подключение намеренно оставлено приложению:
пример сам по себе не подключается к точке доступа.

## Контракты API и память

- Один экземпляр на приложение. `init/register/seal` выполняются последовательно
  одной задачей до запуска обслуживания. Повторный init запрещён; deinit нет.
- Config и command descriptors копируются. Строки, provider functions, context
  pointers и optional `legacy_capabilities` заимствуются и живут до перезапуска.
  Массив `legacy_capabilities` должен завершаться NULL.
- После seal registry неизменяемый. Максимум 24 команды, имя до 48 ASCII
  букв/цифр/`_`/`-`, описание до 160 bytes. Duplicate registration отвергается.
- Выполнение команд защищено atomic gate: параллельный или рекурсивный вызов
  возвращает `ESP_ERR_INVALID_STATE`, без ожидания. Callback короткий,
  неблокирующий; длительные действия передаются собственной очереди приложения.
  `ESP_OK` означает принятие команды, а не завершение физического действия.
- Providers могут вызываться из разных transport/tasks и должны синхронизировать
  свои данные. HTTP данного проекта обслуживается одной задачей. BLE Lock
  использует прежние очереди, ULP snapshots, mutex/critical section журналов.
- JSON provider пишет в переданный buffer и завершает строку NUL; при нехватке
  места возвращает `ESP_ERR_INVALID_SIZE`. Компонент не владеет состоянием provider.
- HTTP JSON snapshot ограничен 8192 bytes, monitor — 16 KiB, command query —
  1023 bytes и максимум 8 аргументов. Дополнительно действует лимит URI самого
  HTTP server. Динамические buffers освобождаются после ответа; cJSON применяется
  только к metadata/capabilities фиксированного registry, а не к входному JSON.
  Если экранированные metadata не помещаются, возвращается ошибка размера.
- Обязательные строки metadata ограничены 96 bytes, build — 96,
  history_url — 512. URI истории просто возвращается в info: компонент не знает
  формата архива, не отправляет туда данные и не загружает их.
- `prepare_restart(ctx)` вызывается после отправки ответа и задержки 1 s.
  После hook до `esp_restart()` оставлено 250 ms. `ota_complete(ctx)` вызывается
  после успешного выбора boot partition. Оба hook необязательны.

## HTTP API v1

| Метод и URI | Результат |
|---|---|
| GET `/api/agent/info` | `agent_api_version`, `device_type`, `device_id`, `device_name`, `firmware_version`, `boot_id`, optional `history_url`/`build` |
| GET `/api/agent/capabilities` | `api_version`, feature flags и список `{name, description}` |
| GET `/api/agent/status` | JSON приложения без навязанной схемы |
| GET `/api/agent/events` | JSON provider событий |
| GET `/api/agent/monitor?since=N` | Текст и headers `X-Log-Cursor`, `X-Log-Oldest`, `X-Log-Dropped` |
| POST `/api/agent/command?action=NAME&value=VALUE` | `{"accepted":true,"action":"NAME"}` |
| POST `/api/agent/restart` | `{"ok":true,"accepted":true}` и отложенный restart |
| POST `/api/agent/ota` | Бинарное тело → esp_ota_begin/write/end/set_boot_partition → ответ → restart |

Аргументы command — URL-encoded пары строк. Имена аргументов произвольные;
`action` зарезервирован. Callback получает `ai_agent_args_t`, читает аргумент через
`ai_agent_arg(args, "value")`. Указатели действуют только во время callback.
Тело command-запроса пока не поддерживается. В capabilities позднее можно добавить
optional описание аргументов, сохранив поля `name`/`description` и API v1.

Неизвестная команда и неверный input: HTTP 400 с `{"error":"..."}`;
занятый dispatcher/очередь: 503; отсутствие provider: 501; ошибка приложения: 500.
Одновременный HTTP OTA/restart отклоняется с 409. OTA проверяет размер раздела,
прерывает upload после трёх последовательных socket timeout и использует
стандартную проверку образа IDF. Текущий HTTPS pull auto-update приложения остаётся
в `wifi_ota.c`; координацию с ним при дальнейшем развитии следует учитывать отдельно.

`agent_api_version` не связан с `firmware_version`. `boot_id` — случайный маркер
инициализации, не credential и не средство аутентификации. Совместимые старые поля
`api_version` и `firmware` сохранены в info. Firmware string может иметь любой формат.

## Совместимость BLE Lock

В `main/lock_ai_adapter.c` остались 11 команд: `open`, `close`, `auto`,
`always_open`, `toggle_hold_open`, `arm_open`, `disable_auto_close`, `ulp_lock`,
`ulp_unlock`, `buzzer`, `diagnostics`; metadata замка; LED/Wi-Fi hooks; прежние
status/ULP JSON formatter. Agent status содержит старые поля status и полный
ULP snapshot в `ulp`. Если snapshot временно недоступен, возвращаются `ulp:null`
и `ulp_error`, а основная диагностика остаётся доступной.

`/api/status` и `/api/ulp` сохраняют прежние схемы, используя те же formatter.
`/api/ota` и `/api/system/restart` — aliases общих handlers компонента.
Сохраняются старые четыре Agent URI, runtime log cursor headers, event log и
совместимый список capabilities в info. Отдельной legacy-копии Agent dispatcher
или HTTP OTA в Web UI больше нет. Calibration wizard, BLE/ULP/автомат и таймеры
не переписывались.

## PowerShell-клиент

```powershell
.\device-agent.ps1 discover
.\device-agent.ps1 info -Device 192.168.1.50
.\device-agent.ps1 capabilities
.\device-agent.ps1 status
.\device-agent.ps1 events
.\device-agent.ps1 monitor
.\device-agent.ps1 command relay_on
.\device-agent.ps1 command set_level -Arguments @{value=42}
.\device-agent.ps1 restart
.\device-agent.ps1 flash firmware.bin
# Прежний синтаксис также работает:
.\device-agent.ps1 -Action flash -Firmware firmware.bin -Device 192.168.1.50
```

Discovery использует ARP-кэш Windows, а не сканирование подсети/mDNS. Последний
адрес сохраняется рядом со скриптом в `.device-agent/connection.json`.
`snapshot` работает с современным generic API; `watch` и `ulp` остаются
специфическими shortcuts BLE Lock. `open`, `close`, `always-open`, buzzer и прочие
aliases вызывают общий `Invoke-AgentCommand`.

OTA читает info до загрузки, отправляет файл, опрашивает info после загрузки,
проверяет стабильный device_id и новый boot_id, печатает previous/current version.
Повторная загрузка той же версии допустима (`version_changed:false`). Timeout
задаётся `-RestartTimeoutSec` (по умолчанию 60 s; отдельный GET может занять 3 s).
Для старого Agent без boot_id используется возврат после недоступности либо
изменение version/build. Если такой reboot нельзя подтвердить, клиент сообщает
ошибку. Полностью до-Agent прошивки не распознаются автоматически по версии.

## Граница безопасности и дальнейшая работа

Текущий API **нельзя публиковать напрямую в Internet**: authentication и
authorization пока отсутствуют. `ai_agent_set_http_authorizer(fn, ctx)` до seal
задаёт единую проверку всех Agent requests и legacy aliases OTA/restart. Hook
получает operation и request, возвращает ESP_OK или отказ (HTTP 403). Политика
доступа не размещается в command callbacks. Другой transport должен применить
свою проверку перед обращением к core. При добавлении защиты всему устройству
нужно также закрыть самостоятельные Web UI/application routes.

Подписи образов, secure boot, flash encryption, rollback/health confirmation и
TLS требуют отдельной настройки штатных механизмов ESP-IDF и ключей приложения.
Компонент не вводит собственной криптографии. Проверка возврата клиентом не
заменяет криптографическую проверку firmware и не доказывает её работоспособность.
Cloud transport, MQTT, WebSocket, fleet management и хранение истории не реализованы.

## Проверки

В корне BLE Lock сборка выполняется `build-esp32.ps1`. Проверки клиента без
оборудования: `python tools/test-device-agent.py`. Они обращаются только к
loopback HTTP-стенду, используют копию скрипта и не меняют сохранённый адрес
реального устройства. Изолированная сборка example проверяет отсутствие
зависимостей от `main/` замка. Результаты и границы проверок — в `TEST_REPORT.md`.
