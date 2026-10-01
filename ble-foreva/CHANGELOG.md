# История изменений

## 2026-10-01 — итог двухчасового теста

- Сохранены экспорт Android `artifacts/android-ble-foreva-export-20261001.txt` и непрерывный Serial-лог `artifacts/esp32-20261001-203809.log`.
- Сопоставлены события сервиса и экрана с 724 блоками ESP32 STAT одной session. За 2 ч 00 мин 31,380 с принято 4895 пакетов, максимальный интервал приема 9185 мс; результаты записаны в `TEST_REPORT.md` и матрицу `TEST_PLAN.md`.
- После завершения теста установлен подготовленный APK с выбором режима advertising; исходные параметры ориентации экрана `0` и `0` восстановлены и проверены. Новая session не запускалась.

## 2026-10-01 — первый диагностический стенд

- Созданы независимые Android advertiser и ESP32-S3 NimBLE scanner.
- Добавлены persistent log, foreground service, RAW/STAT/CSV вывод и план длительных испытаний.
- Android lint выявил необходимость явной обработки потери Bluetooth permission при остановке advertising; обработка добавлена, повторные сборка и lint прошли.
- Добавлен скрипт установки APK с сохранением, восстановлением и повторной проверкой параметров поворота экрана.
- Добавлен скрипт непрерывной записи Serial логов ESP32-S3 в файл с немедленной записью строк на диск.
- APK установлен на физический Xiaomi с Android 16; Activity запущена, `APP_START` подтвержден в persistent log, параметры поворота сохранены.
- Serial capture переведен на pyserial из ESP-IDF Python для обработки переподключения USB Serial/JTAG.
- Отдельная ESP32-S3 прошита через COM8; после сброса проверены запуск NimBLE scan, прием BLE Foreva и запись статистики в файл. Capture пишет в режиме append, чтобы повторный запуск не стирал предыдущий лог.
- Для `STAT` capture больше не отправляет команду в USB Serial/JTAG: штатный режим сканера уже STAT. Устранен цикл `SerialTimeoutException('Write timeout')` при длительной записи; RAW/CSV при невозможности отправить команду теперь явно завершаются с ошибкой.
- Подготовлена следующая Android-сборка с выбором `LOW_POWER`, `BALANCED`, `LOW_LATENCY`; режим активной session сохраняется, показывается в UI/уведомлении и записывается в лог. Текущий установленный APK не обновлялся из-за продолжающегося baseline опыта. Изменены `android/app/src/main/java/com/bleforeva/diagnostic/BeaconState.kt`, `MainActivity.kt`, `BeaconForegroundService.kt`, а также `AUDIT.md`, `README.md`, `TEST_PLAN.md`, `CHANGELOG.md`, `TEST_REPORT.md`.

Созданные файлы: `AUDIT.md`, `CHANGELOG.md`, `TEST_REPORT.md`, `README.md`, `TEST_PLAN.md`, `.gitignore`, `build-android.ps1`, `build-esp32.ps1`, `install-android.ps1`, `capture-serial.ps1`, `capture_serial.py`, `android/settings.gradle.kts`, `android/build.gradle.kts`, `android/gradle.properties`, `android/gradlew`, `android/gradlew.bat`, `android/gradle/wrapper/gradle-wrapper.jar`, `android/gradle/wrapper/gradle-wrapper.properties`, `android/app/build.gradle.kts`, `android/app/src/main/AndroidManifest.xml`, `android/app/src/main/java/com/bleforeva/diagnostic/BeaconState.kt`, `android/app/src/main/java/com/bleforeva/diagnostic/BeaconForegroundService.kt`, `android/app/src/main/java/com/bleforeva/diagnostic/MainActivity.kt`, `esp32/CMakeLists.txt`, `esp32/sdkconfig.defaults`, `esp32/main/CMakeLists.txt`, `esp32/main/main.c`.
