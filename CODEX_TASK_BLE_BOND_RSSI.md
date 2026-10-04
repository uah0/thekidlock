# Задание для Codex: BLE AUTO unlock, RSSI gating и потеря Android bond

## Контекст

Нужно исправить редкий сбой AUTO-открытия замка на ESP32-S3 и потерю BLE bond на Android.

Устройства и версия:
- ESP32-S3.
- Прошивка: `10.0.301`.
- Android: Redmi Note 15.
- Телефон во время проблемного теста физически не перемещался и всё время лежал рядом с ESP.
- RSSI threshold зоны подключения: `-60 dBm`.

Подтверждённый факт, который нельзя переинтерпретировать:
- ESP **не прекращает BLE scanning**.
- Это было проверено ранее и снова подтверждено свежей диагностикой: ESP принимает advertising packets телефона.
- Нельзя объяснять проблему остановкой scanner.
- Нельзя объяснять проблему тем, что пользователь унес/переместил телефон.

Диагностика ESP содержит, например:

```text
BLE LOCK RSSI low n=7/8 min=-69 max=-61 threshold=-60/10s
```

Эта строка означает, что ESP слышала рекламу телефона, но указанные конкретные пакеты были ниже установленного RSSI-порога.

Также около uptime `4390–4398 s` зафиксированы:
- успешные `PEER_SEC ... LTK=1 SC=1`;
- успешные `ENC ... st=0 e=1 ... b=1`;
- успешные `proofRsp ... st=0 enc=1 bond=1 valid=1`;
- позже `proofRsp ... st=257 ... valid=0`;
- затем `encryption/bond rejected after ENC_CHANGE`;
- disconnect;
- после этого Android потерял bond, хотя ESP продолжала хранить bond телефона.

HCI/logcat Android около `10:52:06–10:52:07`:
1. Android сначала запросил сохранённый LTK.
2. HCI показал `ENC_CHANGE status=0 enabled=1`.
3. Следовательно, старый LTK в начале этой BLE-сессии был корректен и реально зашифровал link.
4. После этого Android отправил SMP `Security Request` с `authReq=0x2D`.
5. ESP отправила `Pairing Request` с `authReq=0x09`.
6. Android ответил `Pairing Response` с `authReq=0x2D`.
7. SMP не завершился.
8. Android сообщил `SMP_CONN_TOUT`.
9. Android Bluetooth stack удалил BLE bond.
10. В момент GATT proof Android уже показывал `BOND_BONDING`, поэтому приложение отклонило proof; ESP получила GATT status `257`.

---

# Обязательные правила работы

Перед изменением кода обязательно прочитать и соблюдать корневой `AGENTS.md`.

Критически важные требования `AGENTS.md`:

1. До любых изменений затрагиваемой части проекта выполнить baseline build **проектным скриптом**:

```powershell
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File .\build-esp32.ps1
```

2. До успешного baseline build код не менять.
3. Выполнить аудит относящейся к задаче архитектуры.
4. Актуализировать `AUDIT.md`.
5. После каждого законченного этапа:
   - собрать ESP32 проект проектным скриптом;
   - перечислить изменённые файлы;
   - обновить `CHANGELOG.md`;
   - обновить `TEST_REPORT.md`;
   - не переходить к следующему этапу до успешной сборки.
6. После успешной целевой ESP32-сборки, согласно `AGENTS.md`, загрузить прошивку на текущий замок через существующий `device-agent.ps1`/OTA, если устройство доступно, и проверить возврат устройства и `firmware_version`.

Не менять без доказанной необходимости:
- BLE scanner;
- алгоритм замка;
- Timer A / Timer B;
- owner/NVS модель;
- pairing window;
- Wi-Fi;
- OTA;
- Web UI;
- архитектуру проекта.

Не выполнять массовый рефакторинг.

---

# Файлы, которые нужно исследовать в первую очередь

ESP:
- `main/ble_phone.c`
- `main/lock_control.c`
- `main/lock_control.h`
- `sdkconfig`
- `sdkconfig.defaults`
- связанные NimBLE security/store настройки при необходимости
- `AUDIT.md`
- `CHANGELOG.md`
- `TEST_REPORT.md`

Android:
- `android_lock_beacon/app/src/main/java/com/blelock/app/PhoneBeacon.kt`

Android менять только если анализ докажет, что это действительно необходимо. На данный момент первичный дефект находится в ESP security state machine.

---

# Уже установленная причинная модель

## A. Почему неподвижный телефон может не вызвать новое соединение

В `main/ble_phone.c`, обработка advertising, сейчас есть прямой RSSI gate:

```c
app_config_t cfg;
app_config_get(&cfg);

if (disc->rssi < cfg.phone_rssi_threshold) {
    s_central_low_rssi_count++;
    ...
    return 0;
}
```

В исследованной версии это находится примерно около строк `1698–1709`.

Только пакет, прошедший условие:

```text
disc->rssi >= phone_rssi_threshold
```

попадает дальше к:
- сохранению target;
- `ble_gap_disc_cancel()`;
- `central_connect_target()`.

Следовательно, запись:

```text
BLE LOCK RSSI low n=7/8 min=-69 max=-61 threshold=-60/10s
```

не означает потерю телефона или остановку scanner.

Она доказывает обратное:
- ESP продолжает принимать advertising;
- но эти конкретные измерения не проходят RSSI gate;
- поэтому от них не начинается новый connect.

Телефон при этом может физически не двигаться.

RSSI неподвижной BLE-пары не обязан быть постоянным. Он может колебаться из-за:
- multipath;
- частотного fading между BLE advertising channels;
- ориентации встроенных антенн;
- тела/предметов рядом;
- особенностей TX power / radio scheduling Android;
- локальной интерференции.

Не надо изменять RSSI threshold в рамках исправления security bug, если дополнительные тесты не докажут отдельную необходимость.

### Важное ограничение

RSSI — это только **условие допуска к попытке соединения**.

RSSI НЕ является:
- авторизацией;
- proof;
- командой открыть замок.

---

# B. Как успешный proof должен открыть/удержать замок

В `central_read_cb()` ESP проверяет:

```c
bool secured = ble_gap_conn_find(handle, &desc) == 0 &&
               desc.sec_state.encrypted &&
               desc.sec_state.bonded &&
               peer_has_bond(handle, NULL);
```

После чтения `"BLE_LOCK_V1"` успешная ветка примерно такая:

```c
if (secured && valid) {
    app_config_t cfg;
    app_config_get(&cfg);

    if (s_central_rssi >= cfg.phone_rssi_threshold) {
        lock_feedback_t feedback;
        lock_control_get_feedback(&feedback);

        if (!feedback.requested_open)
            lock_request_open();

        lock_control_report_rssi(true);
    }
}
```

Следовательно:
- успешный encrypted+bonded proof является подтверждением присутствия;
- если исходный advertising RSSI этого сеанса не ниже threshold, ESP вызывает `lock_request_open()`;
- затем вызывает `lock_control_report_rssi(true)` для продления присутствия.

В `main/lock_control.c` хранится `last_sufficient_ms`.

При `LOCK_EVENT_RSSI_HIGH`, если замок уже открыт, обновляется:

```c
last_sufficient_ms = now_ms;
```

При открытом замке без `hold_open` Timer A проверяется примерно так:

```c
bool close_a = !test_open_active &&
               cfg.timer_a_ms > 0 &&
               now_ms - last_sufficient_ms > (int64_t)cfg.timer_a_ms;
```

При истечении Timer A вызывается закрытие.

Отсюда нормальная последовательность возможна даже при неподвижном телефоне:

```text
успешный proof
→ замок открыт / Timer A продлён
→ следующие advertising packets приходят ниже -60
→ новых connect/proof нет
→ last_sufficient_ms больше не обновляется
→ Timer A истекает
→ замок закрывается
```

Scanner всё это время может продолжать работать.

Поэтому нужно отличать:
1. отсутствие нового connect из-за RSSI gate;
2. ошибку security/bond;
3. нормальное закрытие по Timer A при отсутствии новых успешных proof.

---

# C. Почему рядом во времени могут быть и успешные proof, и `RSSI low`

Это не противоречие.

Счётчик `BLE LOCK RSSI low` учитывает подпороговые пакеты.

Неподвижный телефон может дать, например:

```text
-64  reject
-62  reject
-61  reject
-58  accept -> connect -> encryption -> proof
-65  reject
-63  reject
```

Поэтому в близком временном окне могут присутствовать:
- `BLE LOCK RSSI low ...`;
- успешный connect;
- `PEER_SEC LTK=1`;
- успешный `ENC_CHANGE`;
- успешный proof.

`central_rssi` сохраняется из конкретного advertising packet, который прошёл порог и инициировал соединение.

Нужно подтвердить это кодом и логами, но не трактовать агрегированную строку `RSSI low` как утверждение, что абсолютно все пакеты в окне были ниже threshold.

---

# D. Главный security defect

В central AUTO-пути ESP после connect сначала правильно определяет существующий bond.

Фрагмент около `ble_phone.c:1753+`:

```c
int bond_count = 0;
bool bonded = peer_has_bond(s_central_handle, &bond_count);

if (bonded) {
    struct ble_gap_conn_desc desc;
    struct ble_store_key_sec key = {0};
    struct ble_store_value_sec value = {0};

    ...
    ble_store_read_peer_sec(&key, &value);

    event_log_add(
        "BLE central: PEER_SEC rc=%d LTK=%d SC=%d",
        ...
    );
}
```

Для неизвестного peer вне pairing window connect отклоняется, что правильно.

Но затем независимо от того, `bonded == true` или нет, выполняется:

```c
central_set_phase(CENTRAL_PHASE_SECURITY, security_timeout_ms);

int security_rc = ble_gap_security_initiate(s_central_handle);
```

Лог:

```text
BLE central: SEC_START h=... bond=... cand=... rc=...
```

Это находится примерно около `ble_phone.c:1808–1814`.

То есть обычный AUTO connect к уже bonded Android всё равно проходит через общий security-initiate path.

### Что показывает HCI

В проблемной сессии:
- saved LTK был найден;
- Android реально отдал этот LTK;
- `ENC_CHANGE status=0 enabled=1`.

Значит bond и LTK **до повторного SMP были исправны**.

После этого Android отправил:

```text
SMP Security Request authReq=0x2D
```

а ESP ответила новым:

```text
SMP Pairing Request authReq=0x09
```

Android ответил:

```text
Pairing Response authReq=0x2D
```

После чего новый SMP завис и завершился:

```text
SMP_CONN_TOUT
```

Android удалил bond.

Это нельзя диагностировать как "старый LTK был плохой":
старый LTK уже успешно включил encryption.

---

# E. Security configuration

В проекте NimBLE используется конфигурация, эквивалентная:

```c
ble_hs_cfg.sm_bonding = 1;
ble_hs_cfg.sm_mitm = 0;
ble_hs_cfg.sm_sc = 1;
ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
```

Нужно найти точные места/значения в текущей рабочей копии и зафиксировать их в аудите.

Наблюдаемые `authReq`:
- ESP Pairing Request: `0x09`;
- Android Security Request / Pairing Response: `0x2D`.

Это соответствует разным security expectations: Android просит более сильный security level (в частности MITM-related requirement), тогда как ESP сконфигурирована `NO_IO` + `sm_mitm=0`.

Но **не исправлять проблему простым включением `sm_mitm=1`**.

С `NO_IO` это изменит security model и не решит фундаментальную ошибку: штатная AUTO-сессия с уже валидным saved LTK неожиданно превращается в новое pairing.

---

# F. Очень важный ориентир внутри самого ESP-кода

Нужно исследовать другой GAP/security path в `ble_phone.c`, где ESP выступает peripheral/registration side.

В текущем исходнике уже присутствует комментарий примерно около строк `2090+` о том, что проактивный security initiation был убран/не используется, чтобы избежать гонки с реактивным pairing Android.

Это сильный внутренний сигнал, что central AUTO path сейчас нарушает уже принятую в проекте защиту от SMP race.

Codex должен:
1. найти этот комментарий и соседнюю реализацию;
2. сравнить central AUTO path и peripheral/registration path;
3. использовать существующую проектную логику, а не придумывать новую security architecture.

---

# G. Почему `proofRsp ... st=257` появился перед потерей bond

Android `PhoneBeacon.kt`:

```kotlin
val bonded = try {
    device.bondState == BluetoothDevice.BOND_BONDED
} catch (_: SecurityException) {
    false
}

val proofRead =
    wanted &&
    bonded &&
    characteristic.uuid == PROOF_UUID &&
    offset <= PROOF.size
```

Если `bonded == false`, приложение отвечает:

```kotlin
BluetoothGatt.GATT_FAILURE
```

`PROOF_UUID` создан с:

```kotlin
BluetoothGattCharacteristic.PERMISSION_READ_ENCRYPTED
```

То есть proof требует:
1. encrypted GATT transport;
2. Android `bondState == BOND_BONDED`;
3. правильный characteristic/offset.

Во время проблемной сессии Android уже перешёл в `BOND_BONDING` из-за нового SMP.

Поэтому приложение закономерно отклонило proof.

На ESP это проявилось как:

```text
proofRsp ... st=257 ... valid=0
```

Следовательно:

```text
st=257
```

в данном эпизоде — не первичная причина, а следствие того, что Android уже вошёл в новый bonding flow.

Также проверить, что Android app нигде сам не вызывает `removeBond()` или иной явный delete bond. В исследованной версии `PhoneBeacon.kt` такого вызова нет.

---

# H. Почему Android удалил bond

Причинная последовательность, которую нужно считать основной гипотезой и подтвердить кодом:

```text
existing bond
→ saved LTK requested
→ saved LTK accepted
→ link encryption successful
→ Android sends Security Request
→ ESP/NimBLE starts a NEW Pairing Request
→ Android enters BOND_BONDING
→ proof callback rejects read because bondState != BOND_BONDED
→ SMP does not complete
→ Android reports SMP_CONN_TOUT
→ Android Bluetooth stack deletes its local BLE bond
→ ESP still retains its stored bond/LTK
```

После этого появляется asymmetric bond state:

```text
ESP: saved bond существует
Android: bond удалён
```

Это потенциально ломает последующие AUTO connect attempts.

Не удалять автоматически ESP bond только потому, что произошёл такой post-encryption re-pair request: сначала нужно различать настоящий stale key и ошибочный re-pair attempt поверх уже доказанно валидной encrypted session.

---

# Что требуется от исправления

## Главный инвариант

Для обычного AUTO-сеанса с уже известным bonded peer:

```text
saved bond найден
AND
saved LTK успешно включил encryption
AND
connection descriptor показывает encrypted
AND
connection descriptor показывает bonded
AND
peer_has_bond() == true
```

после этого соединение должно продолжиться к service discovery / proof.

Оно **не должно неожиданно переходить в новый pairing flow**.

Новое pairing допустимо только в предусмотренном registration flow для нового peer в активном pairing/BOOT window.

---

# Как искать минимальный патч

Не применять механически первую идею.

Нужно сначала точно изучить текущую версию NimBLE/ESP-IDF 5.4.4 и понять два пути:

1. Как `ble_gap_security_initiate()` ведёт себя для уже bonded peer.
2. Как NimBLE обрабатывает входящий `BLE_GAP_EVENT_REPEAT_PAIRING`, `Security Request`, security elevation и saved LTK.
3. Можно ли штатным API:
   - восстановить encryption только по сохранённому LTK без запуска нового pairing;
   - либо после успешного saved-LTK encryption игнорировать/отклонить нежелательное security elevation, не удаляя хороший bond;
   - либо иначе разделить "restore encryption" и "allow pairing" минимальным изменением state machine.

### Не делать без доказательства

Не ограничиваться слепым:

```c
if (!bonded)
    ble_gap_security_initiate(...);
```

если это оставит bonded connection незашифрованным.

`PROOF_UUID` требует encrypted read, поэтому saved-bond session всё равно должна штатно перейти в encrypted состояние.

Нужно сохранить рабочий LTK restore, но исключить переход в unexpected re-pairing.

---

# Особое внимание: `central_reject_stale_pairing()`

В текущем `ble_phone.c` есть:

```c
static void central_reject_stale_pairing(uint16_t handle, const char *cause)
{
    ...
    esp_err_t delete_err = ble_phone_delete_bond(address);
    ...
    central_disconnect(handle);
}
```

Эта функция удаляет локальный ESP bond и подавляет peer.

Нужно проверить все места, откуда она вызывается:
- `BLE_GAP_EVENT_REPEAT_PAIRING`;
- passkey/security-related события;
- другие ветки.

Проблема:
если текущая session уже доказала валидность saved LTK через успешный `ENC_CHANGE`, последующий неожиданный re-pair request **не является доказательством stale bond**.

Необходимо не допустить уничтожения хорошего bond из-за такого сценария.

Минимальный патч должен явно отличать:
- stale/mismatched bond;
- legitimate new registration pairing;
- unexpected repeat/security elevation после уже успешного LTK restore.

---

# Что не требуется менять

Без нового доказательства не менять:

- continuous BLE scanner;
- advertising parser;
- RSSI threshold;
- lock state machine;
- Timer A;
- Timer B;
- `lock_request_open()` semantics;
- owner assignment;
- owner NVS;
- pairing window duration;
- Wi-Fi provisioning;
- OTA;
- Android service UUID;
- proof payload `"BLE_LOCK_V1"`;
- Android `PERMISSION_READ_ENCRYPTED`;
- Android proof check `bondState == BOND_BONDED`.

Также не считать RSSI авторизацией.

---

# Требуемое логирование для проверки патча

Добавить только минимально необходимую диагностику, если существующей недостаточно.

Для каждого AUTO connection желательно однозначно видеть:

```text
device/session
bond existed before connect?
peer_sec read rc
LTK present?
SC?
security path selected:
    RESTORE_EXISTING
    NEW_PAIRING
ENC_CHANGE:
    status
    encrypted
    bonded
unexpected security/repeat pairing?
proof status
proof valid
central_rssi
disconnect reason
```

Не логировать ключевой материал.

Важно различить:
- bond существовал ДО connect;
- bond появился в ходе текущего registration pairing.

Для этого, если необходимо, добавить небольшой per-connection state flag, например концептуально:

```text
was_bonded_before_connect
encryption_restored_from_existing_bond
registration_candidate
```

Имена выбрать в стиле проекта.

Не добавлять сложную новую архитектуру.

---

# Проверки после патча

Нужны отдельные тесты, чтобы не смешивать RSSI gating, lock FSM и SMP.

## Test 1 — RSSI ниже threshold

Условия:
- телефон уже корректно bonded;
- AUTO включён;
- телефон можно оставить неподвижным;
- добиться advertising RSSI примерно `-61..-70`.

Ожидание:
- ESP продолжает принимать advertising;
- есть `BLE LOCK RSSI low`;
- от подпороговых packets не начинается новый connect;
- Android bond не меняется;
- ESP bond не меняется;
- pairing не запускается.

PASS:
нет ошибочного вывода "scanner stopped".

---

## Test 2 — RSSI выше threshold

Условия:
- существующий bond;
- advertising packet `>= -60`.

Ожидание:

```text
advertising accepted
→ connect
→ existing PEER_SEC/LTK found
→ saved-key encryption
→ ENC_CHANGE status=0 encrypted=1 bonded=1
→ service discovery
→ proofRsp status=0 valid=1
→ lock_request_open / lock_control_report_rssi(true)
```

Не должно быть:
- нового `BOND_BONDING`;
- нового Pairing Request;
- системного pairing UI;
- `SMP_CONN_TOUT`.

---

## Test 3 — неподвижный телефон на границе RSSI

Телефон не двигать.

Наблюдать несколько Timer A periods.

Допустимо:

```text
low RSSI packets
+
отдельные >= threshold packets
+
успешные AUTO sessions
```

Проверить корреляцию:
- каждый успешный proof продлевает `last_sufficient_ms`;
- если достаточно долго нет новых успешных proof, Timer A может штатно закрыть замок;
- закрытие не должно объясняться остановкой scanner.

---

## Test 4 — успешный proof и удержание замка

После:

```text
proofRsp st=0 enc=1 bond=1 valid=1
```

убедиться, что:
- вызывается `lock_request_open()`, если замок ещё не requested open;
- вызывается `lock_control_report_rssi(true)`;
- Timer A отсчитывается от последнего достаточного подтверждения присутствия.

---

## Test 5 — выход и возврат в RSSI-зону

1. Уйти ниже RSSI threshold.
2. Дождаться штатного закрытия по Timer A.
3. Вернуться выше threshold.

Ожидание при возврате:
- saved bond используется;
- saved LTK шифрует соединение;
- proof проходит;
- замок открывается;
- нового pairing нет.

---

## Test 6 — серия AUTO cycles

Выполнить много повторных AUTO connect/disconnect.

Рекомендуемый минимум:
- 100 циклов, если автоматизация позволяет.

На Android параллельно собирать:
- HCI snoop;
- logcat Bluetooth/bond;
- app logs.

На ESP:
- event log.

Критерии PASS:
- ни одного неожиданного Android `BOND_BONDING`;
- ни одного `SMP_CONN_TOUT`;
- ни одного удаления bond Android;
- ни одного нового Pairing Request для уже bonded AUTO peer;
- нет `proof status=257`, вызванного переходом Android в `BOND_BONDING`;
- ESP bond остаётся;
- Android bond остаётся.

---

## Test 7 — reboot ESP

После успешного pairing:
1. reboot ESP;
2. Android bond не удалять;
3. AUTO включён.

Первый AUTO connect должен:
- найти сохранённый PEER_SEC/LTK;
- восстановить encryption;
- прочитать proof;
- не начинать pairing.

---

## Test 8 — настоящий новый pairing

Отдельный контрольный тест:

1. штатно удалить/очистить пару согласованным способом;
2. открыть предусмотренный BOOT/registration window;
3. подключить новый peer.

Только здесь нормальны:
- Pairing Request;
- Android bonding;
- создание нового bond;
- owner assignment по существующей логике.

Это доказывает, что патч не сломал legitimate registration flow.

---

# Проверка Android стороны

`PhoneBeacon.kt` менять не нужно, если не найден отдельный дефект.

Подтвердить:

```kotlin
PROOF_UUID = 6f4f0011-9a7b-4c21-8d32-5e6f708192a3
```

Proof characteristic:

```kotlin
PROPERTY_READ
PERMISSION_READ_ENCRYPTED
```

В read callback:

```kotlin
device.bondState == BluetoothDevice.BOND_BONDED
```

является обязательным условием выдачи proof.

Это желаемое поведение: телефон не должен выдавать proof peer, который сейчас не считается bonded.

Не ослаблять его для маскировки ESP security race.

---

# Требуемый анализ перед кодом

Перед патчем в `AUDIT.md` зафиксировать:

1. Точную текущую цепочку AUTO:
   - ADV;
   - RSSI gate;
   - connect;
   - bond lookup;
   - security initiation;
   - ENC_CHANGE;
   - discovery;
   - proof;
   - lock action;
   - disconnect/retry.

2. Точную registration/pairing chain.

3. Все обработчики:
   - `BLE_GAP_EVENT_CONNECT`;
   - `BLE_GAP_EVENT_ENC_CHANGE`;
   - `BLE_GAP_EVENT_REPEAT_PAIRING`;
   - `BLE_GAP_EVENT_PASSKEY_ACTION`, если участвует;
   - security timeout path.

4. Как текущая версия NimBLE ESP-IDF 5.4.4 выбирает:
   - encrypt using stored LTK;
   - new SMP pairing.

5. Как входящий Android Security Request может привести к новому Pairing Request после уже успешного encryption.

6. Почему current stale-pairing recovery может ошибочно удалить хороший bond.

---

# Предпочтительная форма исправления

Желаемый результат state machine концептуально:

```text
CONNECT
|
+-- peer unknown
|   |
|   +-- registration window CLOSED
|   |      -> reject/disconnect
|   |
|   +-- registration window OPEN
|          -> allow NEW_PAIRING
|          -> require resulting encrypted + bonded session
|
+-- peer already bonded
    |
    -> restore/use existing LTK
    -> require ENC_CHANGE success
    -> encrypted + bonded
    -> DO NOT enter unexpected new pairing
    -> discover/read proof
```

Если после успешного existing-LTK encryption Android присылает Security Request, который требует security level, несовместимый с текущей `NO_IO` / non-MITM AUTO contract:

- не превращать этот штатный AUTO session в re-pairing;
- не удалять локальный хороший bond только из-за этого;
- завершить/отклонить security elevation наиболее штатным способом NimBLE;
- сохранить существующий bond;
- не менять security model всего проекта без отдельного доказательства.

Конкретный API/обработчик выбрать после проверки NimBLE 5.4.4 source.

---

# Что считать корневой причиной

Финальный отчёт должен разделять две вещи.

## Причина отсутствия нового подключения в отдельных временных интервалах

Advertising был принят, но:

```text
RSSI < -60 dBm
```

поэтому текущий RSSI gate не запускал connect.

Это нормально следует из кода и не требует движения телефона.

## Причина потери Android bond

Рабочий saved LTK сначала успешно включил encryption.

После этого возникла ненужная новая SMP-процедура:

```text
Security Request
→ Pairing Request
→ Pairing Response
→ SMP timeout
→ Android deletes bond
```

Следовательно, потеря bond связана с ошибочным повторным security/pairing transition поверх уже успешно восстановленного bonded encrypted link, а не с остановкой scanning и не с физическим уходом телефона.

---

# Acceptance criteria

Исправление принимается только если одновременно выполнено всё:

1. Baseline ESP build до изменений успешно выполнен через `build-esp32.ps1`.
2. `AUDIT.md` обновлён до изменения кода.
3. Изменения минимальны и локализованы.
4. ESP собирается после каждого этапа.
5. `CHANGELOG.md` обновлён.
6. `TEST_REPORT.md` обновлён.
7. Если доступен текущий замок — прошивка загружена штатным способом после успешной сборки и версия проверена.
8. Existing bonded AUTO peer использует сохранённый ключ.
9. Existing bonded AUTO peer не входит неожиданно в новый pairing.
10. Android bond переживает серию AUTO cycles.
11. Legitimate registration pairing продолжает работать.
12. RSSI ниже threshold по-прежнему не запускает connect.
13. RSSI выше threshold допускает normal AUTO connection.
14. Proof остаётся обязательным encrypted+bonded подтверждением.
15. Lock/Timer A алгоритм не изменён.
16. Scanner не перезапускается как "исправление".
17. Owner/NVS/Wi-Fi/OTA/pairing window не изменены без отдельной доказанной причины.

---

# Формат результата от Codex

В конце работы предоставить:

1. **Root cause** — кратко, отдельно RSSI gating и SMP/bond failure.
2. **Что изменено** — по файлам и функциям.
3. **Почему патч минимален**.
4. **Почему хороший existing bond больше не может быть уничтожен этим AUTO flow**.
5. **Как сохранён legitimate new pairing**.
6. **Baseline build result**.
7. **Post-change build result**.
8. **Device flash/OTA result**, если устройство доступно.
9. **Результаты каждого теста**.
10. **Оставшиеся риски/непроверенные пункты**, если они есть.

Не объявлять проблему решённой только по успешной компиляции. Нужна проверка runtime BLE sequence и сохранности Android bond.
