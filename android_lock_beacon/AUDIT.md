# Аудит Android-приложения BLE Lock

## Удаление ADMIN_AUTH — 22.07.2026

- Пароль администратора и UUID `.0005` удалены.
- Панель настроек использует только encrypted + bonded `ADMIN_CONFIG` `.0006`.
- Пароли и собственные ключи приложение не хранит.

Дата: 20.07.2026. Этап A0.

## 1. Исходное состояние

- Каталога Android-приложения и исходного APK до аудита не было.
- Прошивка ESP32 phone-only реализована до этапа 7.
- Пользователь подтвердил работоспособность предыдущего этапа прошивки.
- Финальная аппаратная регрессия этапа 7 остаётся отдельной проверкой и не
  должна смешиваться с разработкой Android-клиента.

## 2. Локальная среда

- ОС: Windows 10.
- Android SDK: `F:\Android\Sdk`.
- Платформы SDK: Android 33, 34, 35, 36 и 36.1.
- Build Tools: 28.0.3, 35.0.0, 37.0.0-rc2.
- ADB: 37.0.0.
- Command-line tools/sdkmanager: 20.0.
- JDK 17: `C:\jdk17`, Temurin 17.0.14; `javac` доступен по полному пути.
- Системный `java` указывает на устаревшую 32-битную Java 8, поэтому сборочные
  команды должны явно использовать `JAVA_HOME=C:\jdk17` и добавлять
  `C:\jdk17\bin` в начало PATH.
- Локально распакован Gradle 8.14 в пользовательском wrapper cache.
- Глобальная команда `gradle` отсутствует.
- Android Gradle Plugin и Kotlin Gradle Plugin в стандартном локальном кэше не
  обнаружены. При недоступном интернете это основной риск первой сборки.

## 3. Проверенный GATT-контракт

- Advertising name: `BLE Lock`.
- Service UUID: `6f4f0001-9a7b-4c21-8d32-5e6f708192a3`.
- INFO UUID: `6f4f0002-9a7b-4c21-8d32-5e6f708192a3`, READ + READ_ENC.
- COMMAND UUID: `6f4f0003-9a7b-4c21-8d32-5e6f708192a3`, WRITE + WRITE_ENC.
- STATUS UUID: `6f4f0004-9a7b-4c21-8d32-5e6f708192a3`, READ + READ_ENC.
- Команда: ровно четыре байта UTF-8/ASCII `OPEN`.
- INFO возвращает `BLE Lock <version>`; STATUS — `OPEN` либо `LOCKED`.
- ESP32 дополнительно проверяет `encrypted`, `bonded` и connection RSSI до
  помещения OPEN в очередь автомата.

Следствие: Android должен сначала подключиться и открыть service discovery.
Обращение к encrypted characteristic инициирует системный bonding при открытом
окне регистрации. Запись OPEN выполняется только после `BOND_BONDED`; окончательное
решение всё равно остаётся на ESP32.

## 4. Выбранная архитектура

- Kotlin, один модуль `app`.
- Android Views и XML вместо Compose: меньше внешних артефактов и ниже риск
  офлайн-сборки.
- `minSdk 26`, `compileSdk/targetSdk 35`: покрывает современные BLE permission
  APIs и использует установленную стабильную платформу/build-tools 35.0.0.
- `MainActivity`: разрешения, состояние, ручная регистрация/OPEN, включение
  автооткрытия, журнал и переход к battery settings.
- `BleLockService`: foreground service типа `connectedDevice`.
- `BleRepository`: единственный последовательный BLE state machine.
- `BleProtocol`: все UUID и байтовые команды в одном месте.
- `LockSettings`: SharedPreferences; bonding keys хранит только Android OS.
- `BootReceiver`: восстановление ранее включённого foreground service.
- Обмен UI/service: broadcasts либо binder без сторонней библиотеки.

## 5. Состояния автомата приложения

`STOPPED -> PERMISSION_REQUIRED -> SCANNING -> CONNECTING -> BONDING ->
DISCOVERING -> READY -> OPEN_SENT -> COOLDOWN/DISCONNECTED`.

- Scan фильтруется по service UUID и останавливается при нахождении.
- После ошибки применяется ограниченный backoff.
- В одном цикле соединения OPEN отправляется не более одного раза.
- Повторный цикл начинается после disconnect/ухода, а ограничения повторного
  физического открытия дополнительно обеспечивает ESP32.

## 6. Планируемые файлы

- `android-app/settings.gradle.kts`, `build.gradle.kts`, `gradle.properties`.
- `android-app/gradlew`, `gradlew.bat`, `gradle/wrapper/*`.
- `android-app/app/build.gradle.kts`, `app/proguard-rules.pro`.
- `android-app/app/src/main/AndroidManifest.xml`.
- `.../MainActivity.kt`, `BleLockService.kt`, `BleRepository.kt`.
- `.../BleProtocol.kt`, `LockSettings.kt`, `BootReceiver.kt`, `AppState.kt`.
- XML layout, strings, colors/themes, notification icons.
- `android-app/README.md`, `CHANGELOG.md`, `TEST_REPORT.md`.

На этапах A1–A4 прошивку менять не планируется. Расширение firmware GATT для
полной BLE-панели относится только к A5.

## 7. Риски

1. Без локального Android/Kotlin Gradle plugin и без доступа к Maven/Google
   repository APK не соберётся, даже если исходники корректны.
2. Системный PATH выбирает Java 8; Gradle/AGP требуют JDK 17.
3. Android-производители ограничивают фоновые процессы; foreground service и
   исключение из battery optimization повышают надёжность, но не дают абсолютной
   гарантии на каждом телефоне.
4. Android 12+ и 13+ имеют разные runtime permissions; старые версии требуют
   location permission для scan.
5. Bonding — асинхронный системный процесс. Нельзя считать успешным только вызов
   `createBond()`; нужно ждать broadcast `BOND_BONDED`.
6. Encrypted characteristic может вернуть authentication error до завершения
   bonding; требуется корректное ожидание и повтор операции, не цикл запросов.
7. В ESP32 разрешено одно соединение. nRF Connect или другой телефон может
   занять его и помешать приложению.
8. RSSI в Android не является решающим: итоговый порог применяется ESP32.
9. Непрерывный low-latency scan расходует батарею; нужны окна scan и backoff.
10. Автозапуск foreground service после boot ограничивается версиями Android;
    manifest и поведение необходимо проверять на целевом телефоне.
11. Удаление bond только на одной стороне приводит к security failure; UI должен
    дать инструкцию удалить bond и в Web UI ESP32, и в Android.
12. До реального телефона нельзя подтвердить открытие из кармана, работу при
    погашенном экране и расход за 8 часов.

## 8. Итог A0

Архитектура и GATT-контракт определены. Изменять прошивку для базового Android
клиента не требуется. Переход к A1 разрешён после представления этого аудита;
первая сборка потребует доступных AGP/Kotlin dependencies либо их установки.

## Актуализация A5.2 — 22.07.2026

- Фактическая прошивка сохраняет `ADMIN_AUTH` UUID `.0005` и требует AUTH после
  encrypted + bonded соединения; прежняя запись об удалении AUTH не
  соответствует текущему коду ESP32.
- `ADMIN_CONFIG` UUID `.0006` передаёт шесть полей:
  `rssi,rssi_hold,long_open_ms,timer_a_ms,buzzer_gpio,open_level`.
- Пароль вводится пользователем, не вшивается и не сохраняется приложением.
- Успешная AUTH действует только в пределах текущего GATT-соединения.
