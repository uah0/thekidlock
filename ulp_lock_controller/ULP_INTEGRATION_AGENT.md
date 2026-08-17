# Инструкция агенту: перенос ULP-контроллера в основной Bluetooth-проект

## Цель

Перенести архитектуру из `ulp_lock_controller` в основной ESP-IDF проект, который управляет электромагнитным замком и получает команды открытия/закрытия по Bluetooth.

Главный принцип: ULP RISC-V владеет силовым контуром и измерением, а Xtensa main CPU принимает Bluetooth-команды, передаёт их ULP и управляет WS2812.

Существующий рабочий Bluetooth-проект нельзя ломать. Перед изменениями создать отдельную ветку или резервную копию и сначала собрать исходное состояние.

## Аппаратные ограничения

Сохранять текущую схему:

- ESP32-S3;
- GPIO8 — gate силового MOSFET;
- GPIO7 — `ADC_SENSE`, ADC1 channel 6;
- GPIO5 — `VBAT_ADC`, ADC1 channel 4;
- INA180A2, gain 50;
- shunt 0.33 Ω;
- делитель VBAT 150 kΩ / 27 kΩ;
- WS2812 DATA на GPIO48.

GPIO8 должен принадлежать только ULP после его запуска. Main CPU не должен использовать GPIO8 напрямую, через PWM, RMT или обычный GPIO API.

GPIO48 не является RTC GPIO, поэтому WS2812 оставляется под управлением main CPU через RMT. ULP не должен пытаться передавать протокол WS2812.

## Что переносить из PoC

Использовать как исходный образец:

- `ulp_lock_controller/ulp/main.c`;
- `ulp_lock_controller/ulp/shared_state.h`;
- `ulp_lock_controller/ulp/experiment_config.h`;
- `ulp_lock_controller/main/controller_api.h`;
- настройки ULP RISC-V из `ulp_lock_controller/sdkconfig.defaults`;
- штатную ESP-IDF интеграцию `ulp_embed_binary()`.

Не копировать build-каталог и сгенерированные файлы. ULP должен собираться штатным CMake-механизмом проекта.

## Владение ресурсами

### ULP владеет

- GPIO8;
- включением и выключением катушки;
- диагностическим импульсом;
- off-guard после выключения;
- чтением ADC_SENSE и VBAT_ADC;
- расчётом fixed-point feature;
- классификацией пластины;
- силовыми fault-состояниями;
- записью результата в RTC slow memory.

### Main CPU владеет

- инициализацией платформы и загрузкой ULP;
- Bluetooth stack и обработчиками команд;
- mailbox-командами для ULP;
- чтением shared state;
- WS2812 на GPIO48;
- пользовательской диагностикой, если она нужна.

После запуска ULP main CPU не должен перенастраивать ADC и GPIO8 в рабочем цикле.

## Shared RTC memory contract

Сохранить seqlock для результатов измерения:

```c
volatile uint32_t sequence;
volatile int32_t  baseline_raw;
volatile int32_t  vbat_raw;
volatile int32_t  sense_raw;
volatile int32_t  delta_raw;
volatile uint32_t normalized_feature;
volatile uint32_t plate_state;
volatile uint32_t fault_flags;
volatile uint32_t measurement_counter;
volatile uint32_t pulse_actual_us;
```

Командный mailbox:

```c
typedef enum {
    ULP_CMD_UNLOCK = 0,
    ULP_CMD_LOCK = 1,
} ulp_command_t;

typedef enum {
    LOCK_STATE_STARTING = 0,
    LOCK_STATE_UNLOCKED = 1,
    LOCK_STATE_LOCKED = 2,
    LOCK_STATE_FAULT = 3,
} lock_state_t;

volatile uint32_t command_sequence;
volatile uint32_t command;
volatile uint32_t command_ack_sequence;
volatile uint32_t lock_state;
```

Команда передаётся только так:

```c
void lock_controller_request(ulp_command_t requested)
{
    uint32_t seq = ulp_command_sequence;
    if (seq & 1u) seq++;
    ulp_command_sequence = seq + 1u;
    ulp_command = requested;
    ulp_command_sequence = seq + 2u;
}
```

ULP принимает команду только если два чтения `command_sequence` совпали и sequence чётный. После принятия ULP записывает тот же номер в `command_ack_sequence`.

## Поведение ULP-FSM

### `ULP_CMD_UNLOCK`

1. GPIO8 LOW.
2. Выдержать `ULP_OFF_GUARD_US`.
3. Снять baseline ADC_SENSE.
4. Снять VBAT_ADC.
5. Переключить ADC обратно на ADC_SENSE и выдержать settling.
6. Подать короткий импульс GPIO8 длительностью `ULP_PULSE_US`.
7. Снять ADC_SENSE.
8. Немедленно выключить GPIO8.
9. Рассчитать feature и `plate_state`.
10. Опубликовать результат в RTC memory.

### `ULP_CMD_LOCK`

1. Не выполнять диагностические импульсы.
2. GPIO8 включить один раз.
3. Удерживать GPIO8 HIGH постоянно.
4. Публиковать `LOCK_STATE_LOCKED`.

Нельзя выключать GPIO8 на короткое время при каждом цикле в состоянии LOCKED.

### Fault safety

При любой ошибке ADC, VBAT, sense или некорректной команде:

- GPIO8 немедленно LOW;
- `lock_state = LOCK_STATE_FAULT`;
- выставить соответствующий `fault_flags`;
- не пытаться удерживать катушку включённой автоматически.

## Интеграция Bluetooth

В обработчике Bluetooth-команды:

```c
case BLE_CMD_LOCK:
    lock_controller_request(ULP_CMD_LOCK);
    break;

case BLE_CMD_UNLOCK:
    lock_controller_request(ULP_CMD_UNLOCK);
    break;
```

После отправки команды Bluetooth-код не должен сам дёргать GPIO8. Состояние подтверждать по `lock_state` и `command_ack_sequence`.

При `LOCK_STATE_FAULT` Bluetooth-слой должен получить ошибку, а не считать команду выполненной.

## LED

WS2812 остаётся на main CPU:

- `PLATE_CONTACT` — зелёный;
- `PLATE_GAP_1` — синий;
- `PLATE_GAP_2` — красный;
- `PLATE_GAP_3` — жёлтый;
- `PLATE_GAP_4` — белый;
- `PLATE_GAP_5` — фиолетовый;
- `PLATE_FAR` / `PLATE_UNKNOWN` — выключен.

Показывать цвета пластины только при `LOCK_STATE_UNLOCKED`. При `LOCKED` можно выключать LED или использовать отдельный цвет замка, но не выдавать старое измерение как свежее.

## Инициализация и reset

До загрузки нового ULP-бинарника выполнить:

```c
ulp_riscv_timer_stop();
ulp_riscv_halt();
ulp_riscv_reset();
```

Это необходимо, потому что RTC-домен и ULP могут пережить отдельный цикл прошивки. Иначе старый ULP может конкурировать с новым кодом за GPIO8 и ADC.

Порядок запуска:

1. остановить/сбросить старый ULP;
2. принудительно установить GPIO8 LOW;
3. настроить ADC для `ADC_ULP_MODE_RISCV`;
4. настроить WS2812;
5. загрузить ULP-бинарник;
6. установить wakeup period;
7. запустить ULP;
8. отправить начальную команду `ULP_CMD_UNLOCK` или безопасную команду, выбранную приложением.

## Сборка и проверка

Сначала собрать основной проект без изменений и сохранить размер/partition layout.

Затем после интеграции:

```powershell
idf.py -B build reconfigure
idf.py -B build build
idf.py -B build -p COM32 flash
```

Проверить последовательно:

1. После прошивки не нажимать RESET вручную.
2. Проверить, что ULP стартует автоматически.
3. Подать `UNLOCK` и убедиться, что GPIO8 выдаёт только короткие импульсы.
4. Подать `LOCK` и убедиться, что GPIO8 остаётся HIGH без периодических провалов.
5. Снова подать `UNLOCK` и проверить возобновление измерений.
6. Проверить пластину: контакт, 1 лист, 2 листа, 3 листа, снятая пластина.
7. Проверить fault-путь при отключённом/невалидном ADC-сигнале.
8. Проверить, что Bluetooth не меняет GPIO8 напрямую.

На время интеграции разрешается добавить компактный лог `lock_state`, `plate_state`, `feature`, `fault_flags`, `command_sequence` и `command_ack_sequence`. Перед эксплуатационной сборкой UART-логи удалить или отключить compile-time флагом.

## Не делать

- не использовать main CPU для ожидания 2 мс диагностического импульса;
- не использовать PWM для диагностического измерения;
- не переключать GPIO8 из Bluetooth callback напрямую;
- не запускать одновременно старый CPU ADC-цикл и ULP ADC-цикл;
- не менять пороги классификации без новых логов;
- не считать команду выполненной только по факту её записи в mailbox;
- не использовать GPIO48 как ULP GPIO;
- не добавлять силовую FSM в main CPU параллельно ULP.

## Целевая автоматика замка

Основная программа передаёт ULP команды `CLOSE` и `OPEN`. Main CPU не управляет GPIO8 напрямую.

### Команда CLOSE

1. В состоянии `UNLOCKED` ULP продолжает сканировать пластину.
2. ULP ожидает состояние `PLATE_CONTACT` (зелёный) или `PLATE_GAP_1` (синий).
3. После обнаружения допустимого положения включает GPIO8 на 300 мс.
4. После 300 мс удержания ULP не выключает MOSFET для повторного измерения.
5. Если исходное состояние было `PLATE_CONTACT` или `PLATE_GAP_1`, ULP переводит автомат в `LOCKED`.
6. Иначе GPIO8 выключается, закрытие отклоняется, автомат возвращается к сканированию.

Важно: отдельная проверка положения после 300 мс удержания не используется. Она требовала бы выключить силовой ток, из-за чего пластина могла отойти и результат становился недостоверным.

### Состояние LOCKED

В состоянии `LOCKED` GPIO8 удерживается HIGH постоянно.

В состоянии `LOCKED` ULP только удерживает GPIO8 HIGH. Проверка тока катушки при включённом MOSFET намеренно не выполняется.

Открытая/оборванная цепь в этом состоянии не классифицируется программой. Для её контроля потребовалась бы отдельная аппаратная защита и повторное согласование методики.

### Команда OPEN

1. ULP выключает GPIO8.
2. Выдерживает время размагничивания/off-guard.
3. Проверяет, что ток через катушку прекратился.
4. При нулевом токе переводит автомат в `UNLOCKED` и возобновляет сканирование пластины.
5. Если ток остаётся выше порога — выставляет `COIL_FAULT` и создаёт событие для main CPU.

Минимальные состояния FSM:

```c
LOCK_FSM_UNLOCKED_SCAN
LOCK_FSM_CLOSE_WAIT_PLATE
LOCK_FSM_CLOSE_ENERGIZE_300MS
LOCK_FSM_CLOSE_VERIFY
LOCK_FSM_LOCKED
LOCK_FSM_OPEN_DISCHARGE
LOCK_FSM_OPEN_VERIFY_CURRENT_OFF
LOCK_FSM_FAULT
```

`plate_state` и `lock_state` не смешивать: первое описывает магнитный зазор, второе — режим автомата и электрическое состояние замка.

## События для main CPU

Сохранить в shared contract:

```c
enum {
    ULP_EVENT_NONE = 0,
    ULP_EVENT_LOCKED = 1u << 0,
    ULP_EVENT_UNLOCKED = 1u << 1,
    ULP_EVENT_COIL_FAULT = 1u << 2,
    ULP_EVENT_CLOSE_REJECTED = 1u << 3,
    ULP_EVENT_OPEN_FAILED = 1u << 4,
};

volatile uint32_t event_sequence;
volatile uint32_t event_flags;
volatile uint32_t fsm_state;
volatile uint32_t coil_current_raw;
volatile uint32_t coil_current_ma;
```

ULP сначала записывает событие в RTC memory. В текущем PoC main CPU обнаруживает новое событие по `event_sequence` при обычном опросе shared memory; software-interrupt ULP не используется.

Не добавлять ISR для ULP только ради этого PoC. Обработку Bluetooth, аварии и WS2812 выполнять в обычной задаче main CPU.

## Важные ограничения текущего PoC

- `ULP_TEST_CYCLE_ENABLE` должен быть `0` в интегрируемой версии.
- Внутренний fault self-test отключён и не переносится.
- UART-лог `ULP_DEBUG` является временным диагностическим кодом и перед production-сборкой удаляется или выключается compile-time флагом.
- При boot/reset GPIO8 принудительно устанавливается в LOW; состояние `LOCKED` автоматически не восстанавливается.
- При запуске ULP main CPU должен очищать RTC shared state, поскольку RTC slow memory может пережить reset.
