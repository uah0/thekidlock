# BLE Foreva

BLE Foreva is a diagnostic prototype. It must not be used as an access-control authentication mechanism.

Цель: проверить, продолжает ли Android телефон передавать BLE advertising после 2, 8 и 24 часов с выключенным экраном. Android работает как advertiser; отдельная ESP32-S3 с внешним питанием постоянно сканирует эфир. Проект не содержит замок, реле, GATT, bonding, секреты и код существующего приложения.

## Структура

| Путь | Назначение |
|---|---|
| `android/` | Отдельное Kotlin приложение `com.bleforeva.diagnostic` |
| `esp32/` | Отдельная прошивка ESP-IDF 5.4.4, NimBLE observer |
| `build-android.ps1`, `build-esp32.ps1` | Локальные сборочные скрипты |
| `TEST_PLAN.md` | Порядок испытаний и форма результатов |
| `AUDIT.md`, `CHANGELOG.md`, `TEST_REPORT.md` | Аудит и история проверок |

## Android: сборка и запуск

Из каталога `ble-foreva`:

```powershell
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File .\build-android.ps1
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File .\build-android.ps1 -Action lint
```

APK: `android/app/build/outputs/apk/debug/app-debug.apk`. Скрипт использует JBR Android Studio, Android SDK и локальные `.gradle-codex`/`.android`. Для установки на физический телефон используйте `install-android.ps1 -Serial <adb-serial>`: он сохраняет оба параметра поворота экрана до `adb install`, восстанавливает их после установки и проверяет повторным чтением. Если подключено одно устройство, параметр `-Serial` можно опустить. Эмулятор не нужен.

В приложении нажать **START BEACON**, разрешить Bluetooth и уведомления. Статус должен перейти в `ACTIVE`; в persistent notification будут session ID и время начала. **STOP BEACON** или STOP в уведомлении останавливает передачу. **COPY LOG** и **EXPORT LOG** выдают файл событий приложения; CLEAR LOG очищает его. UI обновляет счетчик Running только пока открыт, без периодической передачи с CPU.

Приложение позволяет выбрать `LOW_POWER` (baseline), `BALANCED` или `LOW_LATENCY` перед нажатием START. Выбранный режим применяется только к новой session; активный режим показан на экране и в уведомлении, а в Android log записан вместе с `START_ADVERTISING_REQUEST` и результатом callback. При автоматическом пересоздании сервиса сохраняется режим активной session. `LOW_LATENCY` повышает частоту и расход батареи; проверяйте его отдельным опытом после baseline.

На Android 12+ используются `BLUETOOTH_ADVERTISE` и `BLUETOOTH_CONNECT`; на старых версиях — `BLUETOOTH`/`BLUETOOTH_ADMIN`. Android 13+ требует `POST_NOTIFICATIONS` для видимого уведомления. Сервис имеет `FOREGROUND_SERVICE` и `FOREGROUND_SERVICE_CONNECTED_DEVICE`, тип `connectedDevice`; после ручного запуска сразу вызывает `startForeground`, затем `BluetoothLeAdvertiser.startAdvertising`. Постоянный WakeLock отсутствует. Сервис возвращает `START_STICKY`: при пересоздании он пытается запустить новую session и записывает `SESSION_RESTORED_SERVICE`. Это не гарантия восстановления. Автозапуск после reboot не реализован; reboot проверяется отдельно.

## Протокол advertising

Legacy non-connectable advertising: по умолчанию `ADVERTISE_MODE_LOW_POWER`, также доступны `BALANCED` и `LOW_LATENCY`; `ADVERTISE_TX_POWER_ULTRA_LOW` и timeout 0 остаются прежними. В данных нет имени и scan response. Manufacturer ID `0xFFFF` используется **только как экспериментальное значение**, не является присвоенным Bluetooth SIG Company ID и не служит ключом.

Поле manufacturer data: `FF FF 46 4F 52 45 01 DD DD DD DD SS SS SS SS`, где первые 2 байта — ID в little endian; `FORE` — MAGIC; `01` — версия; `DD` — постоянный для установки 4-байтный Device ID в big endian; `SS` — 4-байтный Session ID в big endian. Итого 15 байт поля manufacturer data и AD структуры длиной 17 байт; вместе с обычными flags укладывается в 31 байт legacy packet. Session меняется при новом START, пересоздании сервиса или восстановлении Bluetooth. Android не обновляет payload каждую секунду. `LOW_POWER` не задает гарантированный интервал в 1000 мс: его измеряет сканер.

## ESP32-S3: сборка и прошивка отдельной платы

Из каталога `ble-foreva`:

```powershell
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File .\build-esp32.ps1
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File .\build-esp32.ps1 -Action flash -Port COMx
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File .\build-esp32.ps1 -Action monitor -Port COMx
```

Для полного сохранения вывода на 2/8 часов удобнее отдельный capture скрипт (Ctrl+C завершает запись):

```powershell
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File .\capture-serial.ps1 -Port COMx -Mode STAT
```

Он сохраняет поток в `artifacts/esp32-YYYYMMDD-HHMMSS.log`, сразу сбрасывает каждую строку на диск и добавляет отдельный `HOST_CAPTURE_START` с настенным временем компьютера. Для всех отдельных пакетов выберите `-Mode CSV`; `-Mode RAW` показывает пакеты человеку. Не открывайте Serial Monitor одновременно с capture скриптом на одном COM-порту.

На проверенной плате COM8 режим `STAT` принимается без команды и пригоден для длительного опыта. Запись команды смены режима через USB Serial/JTAG дала timeout; `capture-serial.ps1` явно сообщает об этой ошибке для `RAW`/`CSV`. До проверки входящего Serial канала используйте `-Mode STAT` для 2/8-часовых опытов: он сохраняет максимальный разрыв и текущую тишину каждые 10 секунд.

`COMx` должен принадлежать **отдельной плате сканера ESP32-S3**. Не загружайте этот образ в рабочий замок. Цель задается в `esp32/sdkconfig.defaults` (`CONFIG_IDF_TARGET="esp32s3"`). Консоль по умолчанию — встроенный USB Serial/JTAG ESP32-S3, 115200 бод. На иной разводке консоли задайте порт в конфигурации ESP-IDF.

Сканер использует passive scan, interval 160 и window 160 единиц BLE по 0,625 мс (оба по 100 мс), то есть полный duty cycle в рамках контроллера. Duplicate filtering выключен. ESP32 не экономит энергию и не отправляет advertising. Если scan завершился, он запускается снова и пишет `SCAN_RESTART`. Это снижает пропуски приемника, но прием каждого радиопакета все равно не гарантируется.

После загрузки выводится `# BOOT` с миллисекундами от старта ESP32. По умолчанию режим `STAT`: статистика каждые 10 секунд. Отправьте в Serial Monitor отдельной строкой `RAW`, `CSV` или `STAT` для переключения. `RAW` выводит каждый принятый пакет, включая адрес и весь advertising payload в hex. `CSV` выводит строки `timestamp_ms,device_id,session_id,rssi,gap_ms,address,payload_hex`; строки `#` — маркеры boot/session/режима. Сохраняйте **весь** поток Serial на компьютере на протяжении испытания. В `STAT` есть `current_silence_ms`, поэтому продолжающееся отсутствие пакетов видно и без их возобновления. `max_observed_ms` считает завершенные интервалы между двумя приемами; `max_including_silence_ms` также включает текущую тишину.

Статистика относится к текущей паре Device ID/Session ID. При ее смене накопители сбрасываются и появляется `# SESSION`; для долгого опыта используйте один телефон. `Dropped_serial_rows` должен оставаться нулем при CSV/RAW: переполнение очереди строк и потеря Serial данных искажают внешний CSV, даже когда внутренние счетчики продолжают считать пакеты. ESP32 не имеет RTC: все времена в логах — миллисекунды от загрузки, а не настенные часы.

## Длительные испытания

Для двухчасового теста сначала убедитесь, что сканер показывает `# BOOT`, `# SCAN_START` и первые пакеты. Запустите запись Serial в файл, нажмите START BEACON, зафиксируйте Device/Session ID, отключите зарядку, выключите экран и не двигайте телефон 2 часа. В конце проверьте `max_including_silence_ms`, `Packets_last_60s`, смену session и Android log/notification. Для восьмичасового теста повторите то же на неподвижном незаряжаемом телефоне ночью. Не смешивайте reboot или Bluetooth cycle с этими тестами. Полный порядок — в [TEST_PLAN.md](TEST_PLAN.md).

Большой gap означает отсутствие **принятых** сканером пакетов, а не доказательство отсутствия излучения телефона. Радиопомехи, расстояние, ориентация и потеря логов тоже возможны. Сравнивайте с 30-минутным baseline в том же положении; снимайте RSSI и отдельно измеряйте дистанции 0,25–5 м. Если `current_silence_ms` растет, а Android log показывает живой сервис, проверяйте радиосреду и повторяйте опыт. Android/OEM может останавливать сервис при энергосбережении, force stop, смене Bluetooth или нехватке ресурсов; `START_STICKY` и foreground notification не устраняют эти ограничения.
