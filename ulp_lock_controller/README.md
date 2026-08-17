# ulp_lock_controller

Независимый ESP-IDF 5.4 PoC для переноса диагностического цикла замка на ULP RISC-V ESP32-S3.

## Что выполняет ULP

ULP владеет GPIO8 после запуска. Через mailbox в RTC slow memory main CPU передаёт команды `ULP_CMD_LOCK` и `ULP_CMD_UNLOCK`. В состоянии LOCKED ULP один раз включает GPIO8 и удерживает его постоянно включённым. В состоянии UNLOCKED ULP выдерживает off-guard, читает ADC1 GPIO7/GPIO5, подаёт импульс `2 ms`, читает ADC_SENSE, выключает GPIO8, считает fixed-point `feature = delta_raw * 100000 / vbat_raw`, применяет классификацию и публикует результат.

Main CPU только один раз настраивает ADC/GPIO, загружает ULP, передаёт команды, читает shared memory и управляет WS2812 на GPIO48. GPIO8 main CPU напрямую не переключает.

## Ограничение PoC

ULP получает raw ADC. Внутри ULP не используется float и не выполняется curve-fitting calibration. Поэтому пороги `ULP_FEATURE_*` — стартовое raw-ratio приближение рабочих токовых диапазонов и требуют сопоставления с логами CPU-проекта. Это не утверждение об абсолютных mV.

## Сборка и прошивка

Из корня нового проекта:

```powershell
idf.py -B build build
idf.py -B build -p COM32 flash monitor
```

Команда передаётся вызовом `lock_controller_request(ULP_CMD_LOCK)` или
`lock_controller_request(ULP_CMD_UNLOCK)`. Успешно принятая команда отражается
в `command_ack_sequence`, а режим — в `lock_state`.

Состояния LED: CONTACT зелёный, GAP1 синий, GAP2 красный, GAP3 жёлтый, GAP4 белый, GAP5 фиолетовый, FAR/UNKNOWN выключен.

## Проверка

Для каждого положения собрать минимум 30 строк `ULP,...`: прижато, 1 лист, 2 листа, 3 листа, дополнительные листы и снятая пластина. Сравнить `delta/feature/state` с рабочей CPU-прошивкой при близком VBAT. Если диапазоны не совпадут, сначала корректируются только `ULP_FEATURE_*`, а timing проверяется по стабильности `pulse` и последовательности публикаций.

В этой первой версии CPU reference не запускается одновременно с ULP: GPIO8 и ADC принадлежат ULP. Сравнение выполняется отдельной прошивкой/режимом, чтобы два владельца не мешали измерению.
