# Проверки текущего выпуска — 10.10.2026

## Иконка и оформление кнопок, автосохранение крутилок — 12.10.2026

- PASS baseline: Android debug сборка и ESP signed-dev build 549 до правок.
- PASS: `node tools/test-sound-slider.cjs`; проверены центрирование крутилок,
  привязка всех регуляторов к сохранению на `change` и отсутствие общей кнопки.
- PASS Android: `build-android.ps1 -Action test`; debug/release unit tests.
- PASS Android: signed release APK 1.0.385 собран.
- PASS publication: `app-v1.0.385` опубликован; публичные APK и JSON совпали
  с локальными, SHA-256 APK `f804812a7e4ee2fda632c88aa95f61ccd4384f8135ba3b08a798cd0b667b2a6c`.
- PASS publication: `esp-v10.0.550` опубликован; SHA-256 OTA
  `be3ae455bb9bf8f15370aaf7ad67da298f5416b657e0cbe10e038eb893ddbd06`.
- PASS retention: предыдущие app-релиз 1.0.383 и ESP-релиз/tag 10.0.548 удалены.
- NOT INSTALLED: `adb devices -l` не показал подключённых устройств; системные
  параметры поворота не читались и не изменялись.
- PASS ESP: signed-dev 10.0.550 собрана, подписана и прошла OTA round-trip;
  бинарный образ 0x151000, в разделе приложения свободно 30%.
- PASS OTA: замок перешёл с 10.0.548 на 10.0.550; `/api/status` подтвердил
  build timestamp `2026-10-12 20:20:11`.
- Изменены: `main/web/index.html`, `tools/test-sound-slider.cjs`,
  `android_lock_beacon/app/src/main/java/com/blelock/app/MainActivity.kt`,
  `android_lock_beacon/app/src/main/java/com/blelock/app/InternetOpenProgressButton.kt`,
  `android_lock_beacon/app/src/main/res/drawable/bg_handoff_card.xml`,
  `android_lock_beacon/app/src/main/res/drawable/ic_handoff_revoke_key.xml`,
  корневые и Android `AUDIT.md`/`CHANGELOG.md`/`TEST_REPORT.md`.

## Тактильный отклик крутилок Web UI — 12.10.2026

- PASS baseline: Android debug build 1.0.382 и ESP signed-dev 10.0.547 собраны до изменений; app-раздел свободен на 30%.
- BLOCKED OTA baseline: Agent API `http://192.168.1.54` не отвечает.
- PASS: `node tools/test-sound-slider.cjs` проверяет вызов Android haptics bridge на делениях, браузерный fallback и защиту Android-моста доверенной страницей.
- PASS: `build-android.ps1 -Action test`; debug/release unit tests успешны.
- PASS: ESP signed-dev 10.0.548 собрана; образ 0x151000 байт, свободно 30% app-раздела.
- PASS: подписанный Android release APK 1.0.383 собран.
- NOT INSTALLED: `adb devices -l` не показал подключённого устройства; ориентация экрана не читалась и не менялась.
- BLOCKED OTA: Agent API `http://192.168.1.54` не ответил; ESP 10.0.548 не установлена, `firmware_version` не подтверждён.
- Изменены: `main/web/index.html`, `android_lock_beacon/app/src/main/java/com/blelock/app/LockWebUiDialog.kt`, `tools/test-sound-slider.cjs`, `AUDIT.md`, `CHANGELOG.md`, `TEST_REPORT.md`, `android_lock_beacon/CHANGELOG.md`, `android_lock_beacon/TEST_REPORT.md`.

## Нативная прокрутка и осевая фиксация крутилок — 12.10.2026

- PASS baseline: signed-dev build 545 собран до изменений; app-раздел свободен на 30%.
- PASS: `node tools/test-sound-slider.cjs`; проверки подтверждают нативный `pan-y`, отсутствие отдельной полосы и блокировку вертикальных жестов для значения крутилки.
- PASS: signed-dev build 546 собран; размер 0x151000 байт, свободно 30% app-раздела.
- BLOCKED OTA: до правок и после сборки Agent API по `http://192.168.1.54` не отвечает; сборка 546 не установлена, firmware_version проверить нельзя.
- Изменены: `main/web/index.html`, `tools/test-sound-slider.cjs`, `AUDIT.md`, `CHANGELOG.md`, `TEST_REPORT.md`.

## Ширина полосы прокрутки и размер крутилок — 12.10.2026

- PASS baseline: ESP signed-dev build 542 собран до изменений; в app-разделе свободно 30%.
- PASS: `node tools/test-sound-slider.cjs` проверил ширину сенсорной полосы 72 px, отступ контента 76 px и крутилки 225×225 px.
- PASS: ESP signed-dev build 543 собран; размер 0x151000 байт, свободно 30% app-раздела.
- BLOCKED OTA: `device-agent.ps1` дважды не получил ответ Agent API от `http://192.168.1.54`; сборка на замок не установлена, `firmware_version` проверить невозможно.
- Изменены: `main/web/index.html`, `tools/test-sound-slider.cjs`, `AUDIT.md`, `CHANGELOG.md`, `TEST_REPORT.md`.

## Размер крутилок и обозначение полосы прокрутки — 12.10.2026

- PASS baseline: ESP signed-dev 10.0.539 собрана; baseline OTA не ответила через Agent API. Итоговый OTA ниже успешен.
- PASS: `node tools/test-sound-slider.cjs`; встроенный JavaScript Web UI прошёл `node --check`.
- PASS ESP: signed-dev 10.0.541, RSA-подпись и encrypted OTA round-trip; Agent API подтвердил переход 10.0.538 → 10.0.541.
- PASS: крутилки 173×173 px (20% больше прежних 144×144 px), тест проверяет разметку track/thumb и позиционирование бегунка.
- UI отдельно на телефоне визуально не осматривался.
- Файлы: `main/web/index.html`, `tools/test-sound-slider.cjs`, `AUDIT.md`, `CHANGELOG.md`, `TEST_REPORT.md`.

## Шрифт списка отзыва ключей — 12.10.2026

- PASS baseline Android debug build до правки.
- PASS Android release APK 1.0.381: сборка и установка через ADB успешны.
- PASS: `accelerometer_rotation=0` и `user_rotation=0` сохранены до установки, восстановлены и повторно проверены после неё.
- PASS code review Android SDK 36: multi-choice row использует 18sp `textAppearanceMedium` и минимальную высоту 48dp; список вернулся к этому размеру после снятия span 0.5×.
- Файл приложения: `android_lock_beacon/app/src/main/java/com/blelock/app/MainActivity.kt`.

## Полоса прокрутки и виброотклик крутилок — 12.10.2026

- PASS baseline: Android debug build успешен; ESP 10.0.537 собрана и OTA подтвердила переход 10.0.536 → 10.0.537.
- PASS Android: release APK 1.0.379 собран и установлен на устройство через ADB; `dumpsys package` подтверждает `android.permission.VIBRATE: granted=true`. Перед установкой сохранены `accelerometer_rotation=0`, `user_rotation=0`; после установки оба значения восстановлены и повторно проверены как 0.
- PASS: `node tools/test-sound-slider.cjs`; встроенный JavaScript Web UI проходит `node --check`.
- PASS ESP: signed-dev 10.0.538, RSA-подпись и encrypted OTA round-trip успешны; OTA подтвердила 10.0.537 → 10.0.538.
- Примечание: при проверке списка брокеров один адрес не ответил в срок и исключён из списка этой сборки, остальные 7 прошли проверку.
- UI-жесты и физическую вибрацию на телефоне отдельно не проверял.
- Файлы задачи: `main/web/index.html`, `android_lock_beacon/app/src/main/AndroidManifest.xml`, `tools/test-sound-slider.cjs`, `AUDIT.md`, `CHANGELOG.md`, `TEST_REPORT.md`.

## Web UI — круговые регуляторы — 12.10.2026

- PASS: ESP signed-dev 10.0.536 собрана, RSA-подпись и encrypted OTA round-trip прошли; размер бинарного образа соответствует разделу приложения.
- PASS: `node tools/test-sound-slider.cjs` проверяет загрузку значений крутилок, отображение RSSI, звуковой диапазон 0–120 мс, защитную задержку и сохранение прежних API-параметров.
- PASS: встроенный JavaScript Web UI прошёл `node --check`.
- PASS OTA: после разрыва соединения на первой попытке повторная передача завершилась успешно; Agent API подтвердил переход 10.0.532 → 10.0.536.
- На проверке списка брокеров один адрес не ответил до тайм-аута и не попал в сформированный firmware-список; остальные 7 адресов проверку прошли.
- Интерактивное управление крутилками на телефоне не проверено.
- Файлы задачи: `main/web/index.html`, `tools/test-sound-slider.cjs`, `AUDIT.md`, `CHANGELOG.md`, `TEST_REPORT.md`.

## Короткие MQTT-топики — 12.10.2026

- PASS до изменений: ESP32 signed-dev 510 и Android debug/release unit tests 1.0.370.
- PASS после изменений: ESP32 signed-dev 511; подписанный и зашифрованный OTA-образ проверен штатным скриптом. Android debug/release unit tests, release APK 1.0.371. Обновлённый RISC-V/Unicorn стенд прошёл 47 сценариев, включая отказ от старого `/command`.
- PASS: `device-agent.ps1 flash` установил 10.0.511 поверх 10.0.509; замок вернулся с новой `firmware_version`. В журнале `MQTT crypto selftest PASS`, подписка MQTT готова.
- PASS: `esp-v10.0.511` и `app-v1.0.371` опубликованы и публично проверены штатными publisher'ами. ESP latest содержит один образ и прямые ссылки; прежние ESP/app-релизы удалены. SHA-256 OTA `31b66700a6abfa5126566e5f0f74a1ba8d2b0ce09ef81e746038778cc57e40ca`, APK `ac51e9b43a894c4ae19a99657005b4fe3d21b1e3799d7297ae9e4b75b3fc048a`.
- NOT INSTALLED: телефон `192.168.1.65:33719` отклонил ADB-подключение; APK на нём не установлен, ориентация не менялась. Реальное интернет-открытие новым приложением не проверено.

## Шифрование MQTT — 12.10.2026

- PASS: до изменений собраны ESP32 signed-dev 506 и Android debug 368.
- PASS: итоговая ESP32 signed-dev 509 собрана; RSA-подпись и шифрованный OTA проверены штатным скриптом, SHA-256 OTA `8168eb808c4d3d1b8d85b68e713181af710294a7c72bbd31296e6ae223763dde`.
- PASS: Android debug/release unit tests и debug APK 1.0.369; HKDF-тест сверяет ключ с независимо вычисленным вектором и разделение направлений. SHA-256 APK `8a7a58ff3d2ac0ac4b5ac8498e4aa154bd4b75cbf3f1c5fd8ff40d51d94e13df`.
- PASS: после временной недоступности замок обнаружен; `device-agent.ps1 flash` установил зашифрованную OTA 10.0.509 поверх 10.0.505 и подтвердил возврат с новой `firmware_version`. Журнал запуска содержит `MQTT crypto selftest PASS`, подключение к MQTT и подписку на `/command`.
- PASS: обновлённый RISC-V/Unicorn стенд `tools/test-cloud-auth.py` прошёл 47 сценариев HMAC, владельца, NVS, очереди и PUBACK. На стенде AEAD-конверт подменён; реальный AES-GCM и HKDF проверены самопроверкой запущенной прошивки.
- PASS: `esp-v10.0.509` опубликован штатным publisher'ом после проверки расшифровки, RSA-подписи и совпадения образа со сборкой. Публичный latest asset совпал по SHA-256; GitHub API подтвердил один ESP asset и прямые ссылки в описании. Старый ESP-релиз удалён.
- PASS: подписанный Android release 1.0.370 опубликован как `app-v1.0.370`; штатный publisher сверил подпись, публичные APK/JSON и удалил предыдущий app-релиз. SHA-256 release APK `14ddda058750734f8234c9d3e8c7239ba3e1cc056ae11929ebcdda47231f916e`.
- NOT INSTALLED: ADB `192.168.1.65:33719` отклонил подключение, список устройств пуст; APK на телефоне не установлен, настройки ориентации не менялись.
- NOT VERIFIED ON DEVICE: реальное интернет-открытие и получение зашифрованного ответа требуют обновления приложения на телефоне. Старая версия приложения несовместима с новым MQTT-протоколом замка.

## Подтверждённые результаты 10.0.436

- PASS: ранее выполнена проектная сборка build-esp32.ps1 -Action build -Profile finalize-existing, ESP-IDF v5.4.4.
- PASS: signed app 1 445 888 байт; SHA-256 3ccf1b5cc221f5ea085478ff6f064f503bc5d3149b6d05a4f3b2af188070e3ba.
- PASS: encrypted OTA 1 446 400 байт; SHA-256 d0d242834c6d61e58ed6d6f0422f84b4d742ac308367cc360e13975e1929ea4b.
- PASS: RSA Secure Boot V2 signature, штатный Espressif decrypt round-trip и совпадение signed app.
- PASS: установка через телефонную WebView, POST /api/ota HTTP 200, возврат ESP версии 10.0.436.
- PASS: runtime rom_download_disabled=true, locked=true, fault_flags=0, calibration_required=false; BLE encrypted=true и bonded=true.
- PASS: calibration_valid=true; calibration/threshold значения и /api/config совпали с baseline до OTA.
- PASS: latest esp-v10.0.436, единственный asset encrypted ble_lock_stage1_s3.bin; публичная загрузка HTTP 200 совпала по размеру и SHA-256.
- NOT RUN: физическое открытие и дополнительный power cycle после финализации. AUTO при проверке false, оставлен false.

Свидетельства перенесены в production/verification/.

## Очистка

- PASS: копии текущих signed/encrypted образов в production/esp-10.0.436/ проверены по указанным SHA-256 до удаления сборочных папок и после него.
- PASS: сохранены .security/, Git, исходники, Gradle wrapper, ESP компоненты и проектные скрипты.
- PASS: удалённые материалы перемещены в Корзину; перечни production/cleanup-*-inventory.json.
- Сохранён Android debug APK; SHA-256 4cddcb3b161278b8713c30aae93f60f342f8a44e39579d772430577ae71f0c91. Повторная установка не выполнялась.
- Новые ESP/Android сборки не выполнялись: очистка не меняет исходники; повторная сборка создаёт кэши и увеличивает BUILD_NUMBER.
- Bootstrap пустого нового замка пока не реализован и не тестировался.


## Документация корня — 10.10.2026
- PASS: функциональные документы сохранены в docs/, контракт GATT извлечён до удаления старого плана.
- PASS: README ссылки на перемещённые документы и MQTT protocol обновлены.
- Сборки не выполнялись: изменена только документация; CMakeLists.txt и файлы конфигурации сохранены.

## Проверки OTA только с телефона — этап сборки
- PASS: исходная ESP сборка finalize-existing 10.0.437 и Android debug проектными скриптами. Baseline OTA 437 не выполнялась: на момент сборки WebView недоступна.
- PASS: итоговая ESP finalize-existing 10.0.438, signed app 1 380 352 байта, encrypted OTA 1 380 864 байта, SHA-256 e31a163106c02fed5080fd04b2f95d3a757c90bf8b5a0d3e42c511af015604ca; signature/decrypt round-trip прошли.
- PASS: build-android.ps1 -Action test — 50 debug + 50 release tests, failures/errors=0. Проверки FirmwareUpdateClient/WebUiRequestPolicy покрывают cache marker, AtomicFile/header и источник/передачу firmware.
- NOT RUN на этом этапе: установка 438 и runtime ответы API; Android APK не менялся/не устанавливался.

## OTA только с телефона — runtime 10.0.438
- PASS: private cache телефона побайтово совпал с encrypted OTA; /__app/firmware HTTP200, 1 380 864 байта, корректный encrypted header. Файл подготовлен по ADB для developer-установки; штатное автоматическое скачивание Android нового релиза в этом runtime тесте не вызывалось.
- PASS: вызвана кнопка uploadFwButton «Установить обновление», используется существующий uploadFw() без прямого fetch-injection OTA.
- PASS: после reboot version=10.0.438, uptime=76с, state=LOCKED, fault_flags=0, calibration_required=false, rom_download_disabled=true, BLE encrypted=true/bonded=true.
- PASS: восемь сохранённых calibration/threshold полей и /api/config совпали с baseline до установки.
- PASS: попытка включить прямой download POST /api/wifi/auto-update?enabled=1 — HTTP400 «Обновление доступно только с телефона»; auto_update=false.
- PASS: публикация esp-v10.0.438 latest, единственный encrypted asset, public download/hash проверен publisher. Signed app SHA-256 58cf190a6a9df2ceac857fccfed28a962c7952ce285d3034f49fbe3ba5fa9935.
- NOT RUN: запрос HTTP вне phone runtime session на реальном замке; gate проверен ревью и компиляцией. Физическое открытие и новый power cycle не выполнялись. APK не устанавливался, orientation не менялась.
- Свидетельства: production/verification/phone-only-438/.

- PASS: текущий комплект 438 и существенные build/runtime/publication свидетельства сохранены до повторной очистки build/cache/helper каталогов; инвентаризация production/cleanup-phone-only-438-inventory.json. Секреты и исходники сохранены.

## Интернет-открытие только владельцем — 10.0.440
- PASS: baseline finalize-existing 439 и итоговая 440 проектным build-esp32.ps1; обе сборки успешны, encrypted decrypt round-trip успешен.
- PASS: tools/test-cloud-auth.py — baseline 37/37, после изменения 43/43. Настоящие HMAC/accept/owner predicate: допустимая команда владельца принимается, корректно подписанная команда другого bonded телефона отклоняется без изменения command ID/active; нет владельца, ошибка NVS, некорректный тип owner, некорректный ID и различие public/random identity проверены. Сохранены проверки replay, подписи, времени, NVS ошибок, MQTT фрагментов и PUBACK.
- PASS: зашифрованный OTA 1 380 864 байта; SHA-256 128fb96104f7930f518ef4712bbf45667da13309944f727b92b62b8fa2bc53cb.
- NOT RUN: OTA на замок и реальное MQTT открытие. Телефонная WebView дала timeout; затем пользователь указал собрать для самостоятельной установки. На устройстве новая версия не подтверждена, GitHub не публиковался. Android код/APK не менялись.
- Свидетельства: production/verification/owner-only-440/; комплект production/esp-10.0.440/.

- PASS: публикация latest esp-v10.0.440, проверка подписи RSA Secure Boot V2 и decrypt round-trip перед загрузкой; публичный фиксированный URL Android отдаёт encrypted OTA с SHA-256 128fb96104f7930f518ef4712bbf45667da13309944f727b92b62b8fa2bc53cb. GitHub latest и размер 1 380 864 байта проверены. Свидетельства public-release.json и publish.log в production/verification/owner-only-440/. Runtime установка по-прежнему не выполнялась.

## Индикатор обновления Android — build 253
- PASS: исходная и итоговая debug-сборки проектным build-android.ps1.
- PASS: build-android.ps1 -Action test; проверки различия номеров, отсутствия downloaded version, отдельных lock keys, нормализации BLE адреса и разрешённого release URL.
- PASS: JS сценарии actual firmware_transfer.js в Node VM: mark выполняется перед native OTA fetch; при network failure сохраняется; неизвестный номер блокирует передачу; status GET и другой origin не меняют отметку.
- PASS: APK установлен на физический телефон; accelerometer_rotation/user_rotation сохранены до установки и восстановлены/проверены после, оба 0.
- PASS: приложение самостоятельно скачало актуальный GitHub encrypted OTA 10.0.440; hash 128fb96104f7930f518ef4712bbf45667da13309944f727b92b62b8fa2bc53cb, downloaded_version=10.0.440. Кэш не подменялся по ADB.
- NOT RUN: передача на настоящий ESP (пользователь устанавливает сам), runtime проверка исчезновения надписи после реального POST. Видимость надписи на главном экране ожидает доступности экрана приложения.

## Итог проверки индикатора — Android 255
- PASS: итоговый debug build 255; 53 debug + 53 release unit tests, failures/errors=0; assembleDebugAndroidTest успешно (через окружение проектного скрипта).
- PASS: физический WebView instrumentation против loopback fixture: private encrypted image 1 380 864 байта передан кнопкой, JS/native bridge сохранил фиктивную отметку; mock ESP ответ проверен. Реальный ESP endpoint не вызывался.
- PASS: UI hierarchy главного экрана содержала «Настройки\nдоступно обновление»; размер второй строки задан RelativeSizeSpan(0.6).
- PASS: после пользовательского начала реальной передачи в preferences появились downloaded_version=10.0.440 и sent_<BLE peer>=10.0.440. Оба номера равны; отметка не зависит от версии/подтверждения ESP.
- PASS: temporary fixture state восстановлен; исправленный runner выполняет cleanup до finish(), instrumentation APK пересобран. Повторный runtime прогон cleanup не выполнялся. Reverse tcp:80 и host fixture остановлены.
- PASS: после всех установок APK значения accelerometer_rotation/user_rotation восстановлены и проверены, 0/0.
- Реальную установку версии 440 на ESP агент не выполнял и не подтверждал. Предыдущие NOT RUN для вида надписи/сохранения отметки заменены приведёнными runtime результатами.

## Названия главных кнопок — Android build 257
- PASS: baseline 256 и итоговая 257 успешно собраны build-android.ps1, итоговая сборка 46с.
- PASS: APK установлен и запущен на физическом телефоне; UI hierarchy содержит все три новых названия и «Сборка 257».
- PASS: скобки в строковом ресурсе firmware_update_available, только подпись получает Color.BLACK и RelativeSizeSpan(0.6).
- PASS: accelerometer_rotation/user_rotation сохранены перед установкой, восстановлены и проверены после установки/запуска, оба 0.
- Unit tests повторно не запускались: изменение ограничено текстом и цветом. Runtime подпись обновления сейчас скрыта, поскольку downloaded и sent совпадают; её цвет проверен по коду.
- Сведения сохранены в production/verification/button-names-257/, актуальный APK — production/android-debug/.

## Упрощение Web UI — ESP 10.0.442
- PASS: baseline finalize-existing 441 и итоговая 442 проектным build-esp32.ps1, signed/encrypted round-trip успешен.
- PASS: Node проверил синтаксис всех script блоков страницы; ссылок serialLog и удалённого пояснения нет. git diff --check успешен.
- PASS: publisher проверил RSA signature, decrypt round-trip, compiled version и public latest SHA-256. Encrypted OTA 1 380 864 байта, hash 0db1d15ba5bea665b2453f7412ff7a84a5e1a5ff1eba562a71c7ec4437448fb3; latest esp-v10.0.442.
- NOT RUN: установка 441/442 на ESP и просмотр новой страницы на устройстве. По последнему указанию установку выполняет пользователь. Android код/APK не менялись.
- Свидетельства production/verification/web-cleanup-442/; актуальный комплект production/esp-10.0.442/.

## Только последний GitHub релиз — 10.10.2026
- PASS: исходный publisher AST и итоговые Python импорты/синтаксис; целевые ESP/Android сборки не выполнялись, host publisher не изменяет firmware.
- PASS: tools/test-release-retention.py — 6/6: сохранение актуального release/tag при draft-дубликате, удаление orphan tags, mismatch latest без удалений, защита newer version, остановка при concurrent latest change, пагинация >100 и запрет чужого repository.
- PASS: реальные GitHub REST удаления; итоговый список содержит единственный release esp-v10.0.442, один asset и единственный tag refs/tags/esp-v10.0.442. Удалены 11 release записей и 10 tags.
- PASS: public latest asset до и после очистки совпал по SHA-256 0db1d15ba5bea665b2453f7412ff7a84a5e1a5ff1eba562a71c7ec4437448fb3. Файл доступен для Android по прежнему фиксированному адресу.
- PASS: git diff --check для изменённых tools.
- Свидетельства production/verification/release-retention/: before.json, after.json, cleanup.log, tests.log. Новый релиз для изменения publisher не создавался, ESP не прошивался.

## Окно регистрации в кнопке — ESP 10.0.444
- PASS: baseline ESP 443 и итоговая 444 проектным build-esp32.ps1 finalize-existing; encrypted round-trip успешен.
- PASS: actual page JS syntax и функции loadPairing/allowPairing в Node VM с имитацией API: POST открывает окно; кнопка показывает 60/59/1 с, после allowed=false исходное название, при ошибке сообщает потерю связи. Две соседние надписи отсутствуют, git diff --check успешен.
- PASS: публикация latest esp-v10.0.444, RSA signature/decrypt round-trip и public asset SHA-256 3f02ed12d3cfd94a0940ae9d87e0651eb3ac150cc882dab96c8c2f3b805039b3; encrypted OTA 1 380 864 байта. Автоочистка оставила только последний release и его tag.
- NOT RUN: установка 443/444 на ESP и реальное открытие окна регистрации. По указанию пользователя установку выполняет самостоятельно; Android APK не менялся.
- Свидетельства production/verification/registration-444/, комплект production/esp-10.0.444/.

## Проверка главного экрана handoff — 10.10.2026
PASS: исходная сборка 258, сборки с новым UI 259 и 260 проектным build-android.ps1. PASS: 106 unit tests (debug/release), 0 failures/errors. PASS: сборка 259 установлена на физический телефон через ADB --no-streaming после восстановления Wi-Fi соединения; rotation сохранены, восстановлены и повторно прочитаны — 0/0. Визуальная проверка на устройстве пока ограничена блокировкой экрана; скриншот показывает keyguard, а не приложение. Переключение режимов и отправка команд реальному замку в этой задаче не выполнялись. Проверены привязки существующих обработчиков, отсутствие изменения BLE/MQTT/OTA, переносы текста и динамическая высота footer.
PASS: финальная сборка 261 (42с), APK установлен на физический телефон; accelerometer_rotation/user_rotation снова восстановлены и прочитаны 0/0. Актуальный APK — production/android-debug/app-debug.apk, доказательства — production/verification/ui-handoff-261/.

## Пять подписей интернет-открытия — 10.10.2026
PASS: Android baseline 262, сборки 263/264; ESP finalize-existing baseline 445 и итоговая 446. PASS: 108 Android unit tests до уточнения подписей, 0 failures/errors. PASS: 47 сценариев real mbedTLS/cJSON HMAC/NVS, включая publish_result с door=open/closed/unknown, отказ при подмене подписанного door и отзыве BLE-ключа. Тестовый стенд дополнен новым состоянием; первая компиляция стенда требовала объявления s_door, исправлена и проверки прошли. PASS: RSA Secure Boot signature и полный encrypted OTA round-trip. PASS: публичная latest загрузка 446, размер 1380864, SHA256 eb9beeac66b2420e5e1ea56830f93f8809732e2faffc1f31517d2f6a34b334c5. Временный кеш GitHub latest на 444 прошёл; обычный URL проверен повторно без cache-busting. На GitHub оставлен один релиз.
Android 263 установлен, ориентация восстановлена 0/0. Скриншот собственного приложения подтверждает читаемую динамическую подпись; интернет-команду в этот момент запускал пользователь, агент команду замку не отправлял. Установка ESP446 агентом не выполнялась.Артефакты — production/esp-10.0.446 и verification/internet-labels-446.
PASS: итоговая Android сборка 265 (45с), 110 unit tests без ошибок (59с). APK265 установлен на физический телефон; ориентация сохранена/восстановлена/проверена 0/0. В видимом тексте осталось ровно пять заданных подписей; ожидание closing/ошибка сохраняют предыдущий этап, что проверяется отдельным тестом reducer. Самостоятельная установка ESP446 остаётся за пользователем.

## Диагностика отсутствия «Дверь открылась» — 10.10.2026
В private firmware_transfer телефона downloaded_version=10.0.446, последняя отмеченная передача текущему замку sent=10.0.444. Прошивка 444 публикует шесть полей без door; новый Android не подменяет closing подтверждением открытия двери. Данные для этого этапа добавлены в уже опубликованной ESP446, требуется пользовательская установка этой версии. Фактическая версия ESP не подтверждена: сохранённый адрес 192.168.1.54 недоступен. Изменений функционального кода не выполнено. Диагностические baseline Android266 и ESP447 успешно собраны, не устанавливались и не публиковались; рабочий APK265 и публичная прошивка446 сохранены. Изменены автоматически только BUILD_NUMBER/Android BUILD_NUMBER и отчёты.

## Замена названия ESP32 на замок — 10.10.2026
PASS: baseline Android267 и ESP448, итоговые Android268 (42с) и ESP449 штатными проектными скриптами. PASS: XML валиден; пользовательские строковые литералы Android/resources/Web UI не содержат ESP32; JavaScript обоих Web UI файлов проверен node --check. PASS: подписанный encrypted OTA449 расшифрован и сравнен с signed plaintext, RSA подпись verified. PASS: public latest449, SHA256 6ab5bccf888e7a544754a00063eb90645235a8723e82ef51b538034c87b312f4, размер1380864. Publisher оставил единственный актуальный релиз.
BLOCKED: APK установка отменена телефоном, INSTALL_FAILED_USER_RESTRICTED: Install canceled by user. accelerometer_rotation/user_rotation сохранены перед попыткой, восстановлены и повторно прочитаны 0/0. APK268 сохранён; установку и изменение интерфейса на физическом устройстве не подтверждаем. ESP449 не прошивалась агентом, пользователь устанавливает сам. Unit tests повторно не запускались: изменения только текста, управление/протоколы/алгоритмы не менялись.

Повторная установка по указанию пользователя: APK268 успешно установлен через ADB --no-streaming (Success), MainActivity запущена. accelerometer_rotation/user_rotation перед установкой сохранены, после установки/запуска восстановлены и проверены: 0/0. Предыдущее ограничение установки снято.

## Новый Web UI / default звука в NVS — 10.10.2026
PASS: baseline ESP450 и итоговая451, build-esp32.ps1 -Profile finalize-existing. PASS: JavaScript syntax и VM проверки реальных функций HTML: slider/API -30/-68/-127, сохранение выбранного отрицательного RSSI и неизменного timer_a_ms, загрузка/сохранение departureBuzzer on/off без инверсии; loadStatus/loadUlp без обращений к удалённым DOM ID, аккумулятор и retained данные; endpoint testOpen сохранён; OTA/registration DOM ID сохранены. PASS: девять сценариев настоящего app_config_init, извлечённого из исходника и выполненного RISC-V/Unicorn с моделью NVS: новый namespace/нет ключа, сохранённый0/1, ошибки get/set/commit/open, повторная инициализация без дополнительных записей.
PASS: RSA signature и encrypted OTA round-trip; public latest451, размер1315328, SHA256 d6f2320a65e373f6a5c57e80e27b3cd125a3be22602f37e2555cf33e93845faf. Publisher оставил один релиз. Реальные NVS/экран нового Web UI на замке не проверялись: прошивку451 устанавливает пользователь. Артефакты — production/esp-10.0.451 и production/verification/web-layout-451.

## Виджет автооткрытия — Android 270, 10.10.2026
PASS: исходная сборка 269 и итоговая 270 проектным build-android.ps1; build-android.ps1 -Action test (debug/release). APK 270 установлен на физический телефон 192.168.1.65:43331, ADB вернул Success. Сохранены и восстановлены accelerometer_rotation=0 и user_rotation=0, повторное чтение подтвердило оба значения. dumpsys appwidget подтвердил регистрацию com.blelock.app.AutoOpenWidget. Проверены пути холодного запуска (onResume), существующего singleTask (onNewIntent), запроса BLE-разрешений и обновления состояния всех виджетов через LockSettings. Нажатие виджета на домашнем экране и физическое автооткрытие не проверялись: добавление виджета выполняет пользователь, аппаратную команду во время проверки не запускали. APK сохранён в production/android-debug/app-debug.apk.

## Android 272 — проверка добавления виджета, 10.10.2026
PASS: исходная сборка 271 и итоговая 272 проектным build-android.ps1. APK 272 установлен на физический телефон, Success. Системные accelerometer_rotation/user_rotation сохранены и восстановлены, проверено 0/0. UIAutomator подтвердил кнопку добавления в приложении. Нажатие этой кнопки на физическом MIUI вызвало размещение: dumpsys appwidget подтвердил новый экземпляр id=67 с provider com.blelock.app.AutoOpenWidget, host com.miui.home, views=android.widget.RemoteViews и активным callback. Подтверждена установка виджета через системный API; причина отсутствия в исходном каталоге MIUI не установлена. Автооткрытие/команды замку проверкой не запускались. Повторный запуск всех unit-тестов не требовался для кнопки системного добавления; предшествующие debug/release тесты сборки 270 успешны. APK сохранён в production/android-debug/app-debug.apk.

## Android 274 — фоновое переключение виджетом, 10.10.2026
PASS: базовая сборка 273 и итоговая 274 проектным build-android.ps1. PASS: build-android.ps1 -Action test, debug/release. APK 274 установлен на физический телефон, Success; accelerometer_rotation/user_rotation восстановлены и проверены 0/0. UIAutomator зафиксировал размещённый виджет с надписью «Включено · выключить». Логи BleLockService на физическом телефоне подтвердили ACTION_AUTO_OPEN_TOGGLE: 15:05:26 false, 15:05:34 true, 15:05:51 false. Прямая цель PendingIntent — приватный BleLockService; запуск Activity из виджета удалён. На телефоне происходили параллельные действия пользователя, поэтому устойчивое сохранение фокуса домашнего экрана не проверено изолированно; старое поведение getActivity исключено кодом. Физическое открытие двери отдельно не проверялось. APK сохранён в production/android-debug/app-debug.apk.

## Android 277 — компактная кнопка виджета, 10.10.2026
PASS: базовая сборка 276 и итоговая 277 проектным build-android.ps1. APK установлен на физический телефон, Success; accelerometer_rotation/user_rotation восстановлены и проверены 0/0. UIAutomator подтвердил отсутствие текста кнопки, contentDescription «Добавить виджет на главный экран» и расположение ниже «Настройки замка»: после прокрутки настройки [45,1528][1035,1753], кнопка [878,1786][1035,1921] (56×48 dp). Проверена полная доступность кнопки прокруткой. Само добавление повторно не запускалось, чтобы не создавать дубликат; существующий обработчик перенесён без изменения API. Повторные unit-тесты для визуального переноса не добавлялись. APK сохранён в production/android-debug/app-debug.apk.

## Android 279 — оформление автооткрытия, 10.10.2026
PASS: исходная сборка 278 и итоговая 279 проектным build-android.ps1; git diff --check. APK 279 установлен на физический телефон, ADB Success; accelerometer_rotation/user_rotation сохранены, восстановлены и проверены 0/0. UIAutomator подтвердил отсутствие лишних видимых подписей и сохранение SeekBar с contentDescription мощности. Снимок экрана auto-ui-279.png просмотрен: название/переключатель и полоса без статуса и подписей, полоса расширяется вправо, компактная кнопка виджета расположена ниже всех основных кнопок, весь основной экран помещается. Текущий уровень мощности при проверке не менялся; BLE-команды и физическое открытие не запускались. Новые unit-тесты для визуального рисования не добавлялись. APK сохранён в production/android-debug/app-debug.apk; XML/PNG и сохранённые значения ориентации — в .device-agent.

## Android 281 — виджет как иконка приложения, 10.10.2026
PASS: исходная сборка 280 и итоговая 281 проектным build-android.ps1; git diff --check. APK 281 установлен на физический телефон, Success. accelerometer_rotation/user_rotation сохранены и восстановлены, проверено 0/0. dumpsys appwidget подтвердил обновлённые размеры провайдера, resizeMode=0 и существующий экземпляр с обновлёнными RemoteViews. В последних логах не найдено ошибок RemoteViews/InflateException. По ресурсам проверены единственная видимая подпись app_name, системный размер изображения и прямое соответствие enabled→ic_launcher_open, disabled→ic_launcher. Визуальное сравнение с соседними иконками на домашнем экране и переключение двух рисунков на телефоне отдельно не подтверждены: текущая страница не показала виджет при обходе. Физическое автооткрытие не запускалось. Новые unit-тесты для смены рисунка не добавлялись. APK сохранён в production/android-debug/app-debug.apk.

## ESP 10.0.453 — uptime в настройках, 10.10.2026
PASS: исходная 452 и итоговая 453 собраны проектным build-esp32.ps1 -Profile finalize-existing. PASS: проверка RSA-подписи и расшифровки encrypted OTA. PASS: JavaScript syntax, git diff --check; Node VM исполнил реальный loadStatus/formatUptime/DOM для 0, 59, 60, 3600, 86400 и 90061 секунд, проверил обновление времени/версии и сброс в «—» при HTTP-ошибке. Публикация 453 успешна; публичный latest asset проверен по SHA-256 fc4f2972327f354f8b1e97564d444f558f57b7e6dc225ed436e42bc979b4974b. Encrypted OTA 1315328 байт, подписанный образ 1314816 байт, свободно 33% OTA-раздела. Предыдущий релиз и тег удалены только после проверки нового latest. Артефакты production/esp-10.0.453, результаты production/verification/web-uptime-453. Реальная установка на замок и проверка его uptime не выполнялись: пользователь устанавливает с телефона самостоятельно. Android не менялся.

## Android 283 — метка ярлыка, 10.10.2026
PASS: базовая сборка 282 и итоговая 283 проектным build-android.ps1; git diff --check. XML проверен: TextView отсутствует, FrameLayout содержит основную иконку с прежним id widget_icon и второй ImageView 16 dp с layout_gravity bottom|left. APK 283 установлен на физический телефон, Success. Системные accelerometer_rotation/user_rotation восстановлены и проверены 0/0. dumpsys appwidget подтвердил зарегистрированный провайдер с новой минимальной высотой 48 dp. Отдельный просмотр размещённого виджета и переключение режима при этой проверке не выполнялись; BLE/физические команды не запускались. Новые unit-тесты для оформления не добавлялись. APK и output-metadata.json сохранены в production/android-debug.

## Android 285 — размер значка виджета, 10.10.2026
PASS: базовая 284 и итоговая 285 собраны проектным build-android.ps1; git diff --check. APK 285 установлен на физический телефон, Success. accelerometer_rotation/user_rotation восстановлены и проверены 0/0. UIAutomator подтвердил widget_icon [328,1596][497,1765], 169×169 px (60 dp при 450 dpi), вместо прежних 135×135 px. Снимок icon-size-285.png просмотрен: значок виджета рядом с обычной иконкой TheKidLock соответствует ей по ширине/высоте; стрелка ярлыка сохранена, подписи под виджетом нет. Переключение автооткрытия при проверке не запускалось. Новые unit-тесты для размера не добавлялись. APK/output-metadata.json сохранены в production/android-debug, скриншоты/XML/настройки ориентации — .device-agent.


## Android 288 — проверка механизма обновления, 10.10.2026
PASS: исходная сборка 286 и итоговая debug 288 проектным build-android.ps1. Сборка 287 выявила неверное использование Intent.setClipData; исправлено через apply, сборка 288 успешна. PASS: build-android.ps1 -Action test — debug и release unit-тесты. Новые 7 тестов проверяют целостность потока, обрыв/лишние байты/подмену, отмену, ограничения JSON, имена тегов, HTTPS-источники и соответствие пакета/версии/сертификата; существующие тесты также прошли. PASS: 7 Python-тестов очистки ESP-релизов и 6 тестов очистки app-релизов, включая сохранение другого типа и остановку при конкурирующей публикации. PASS: git diff --check; ключ и keystore.properties игнорируются Git. Merged manifest подтвердил приватный FileProvider с узким files-path и WorkManagerInitializer directBootAware=false; BLE direct boot сохранён. Release APK, публикация и телефон проверяются следующим этапом; текущий ADB-адрес недоступен.


## Android release 289 — проверка подписанного APK, 10.10.2026
PASS: build-android.ps1 -Action release (включая R8, lintVital и resource shrink). PASS: apksigner verify и проверка сертификата против постоянного JKS; aapt подтвердил com.blelock.app, versionCode=289, versionName=1.0.289, отсутствие debuggable. Оба набора unit-тестов предыдущего этапа содержат по 62 теста, failures/errors/skipped=0. Первая установка на подключённый телефон начата с сохранением ориентации.


## Android 290 — GitHub и физический телефон, 10.10.2026
PASS: release 290 собрана build-android.ps1 -Action release; apksigner/aapt/сертификат JKS проверены publisher. SHA-256 APK cf3771dd9c36be87639af0fcb5ad111add95b68bc23327962e1811f311fb5e22. Публичные APK и JSON перечитаны без авторизации и совпали с локальными; предыдущий app-релиз и тег удалены, ESP 453 остался global latest.
PASS: первая release 289 установлена на физический Android 36 через ADB (удаление debug и установка: Success). accelerometer_rotation и user_rotation сохранены, восстановлены, проверено 0/0. dumpsys подтвердил 289/1.0.289. На телефоне 289 нажата ручная проверка: появился текст «Доступно обновление приложения 1.0.290 · Установить», значит скачивание, проверка SHA/размера/пакета/версии/подписи и приватное сохранение завершились успешно. dumpsys notification подтвердил уведомление id=2002, channel=app-updates. Разрешение POST_NOTIFICATIONS предоставлено для проверки уведомления. Проверка установки через UI продолжается; на экране показано штатное предупреждение MIUI разрешения установки из TheKidLock.


## Android 290 — итог проверки установки, 10.10.2026
PASS: на телефоне пройден штатный путь установки скачанного APK, включая разрешение MIUI и системный установщик. ADB install для перехода 289→290 не использовался. dumpsys package подтвердил versionCode=290, versionName=1.0.290; экран приложения показывает «Сборка 290» и снова «Проверить обновление приложения». Активное уведомление id=2002 отсутствует. accelerometer_rotation/user_rotation после обновления 0/0, как до первой установки. Физические команды замку не запускались.
PASS: независимая публичная проверка GitHub API показывает ровно два релиза и два тега: app-v1.0.290 и esp-v10.0.453. Global latest остался esp-v10.0.453. Свидетельства интерфейса сохранены в .device-agent/app-update-290-*.xml; исходный release APK 289 и опубликованный 290 — в production/android-release-289/290. Реальное ожидание шестичасового фонового интервала и закрытая файловая система release-приложения отдельно не инспектировались; проверены выполненная загрузка, уведомление и установка на устройстве. Настройки сброшены только при разрешённом первом удалении debug APK; переход между release-версиями выполнен поверх приложения.


## Android 292 — regression MQTT release, 10.10.2026
PASS: базовая release 291 без изменений и итоговая release 292 собраны проектным build-android.ps1 -Action release, включая R8/lintVital. Воспроизведено на установленной 290: реальная фабрика из APK вернула java.util.MissingResourceException: Error locating the logging class (INSTRUMENTATION_CODE: 0). Фактический JAR Paho 1.2.5 проанализирован javap: динамическое создание Class.forName/Class.newInstance. До исправления R8 mapping показал исчезнувший конструктор logger и удалённый MqttClient, после исправления MqttClient сохранён.
PASS: APK 292 проверен apksigner/aapt и сертификатом постоянного JKS, затем установлен через ADB -r: Success. dumpsys package подтвердил versionCode=292/versionName=1.0.292. PASS: tools/check_android_mqtt_release.py на 292 — фактическая оптимизированная фабрика вернула logger, INSTRUMENTATION_CODE: -1. Тестовая instrumentation подписана тем же ключом, временный пакет удалён. Значения accelerometer_rotation/user_rotation до и после установки/теста сохранены и проверены 0/0. Тест не обращается к MQTT-брокеру и не отправляет команду открытия; фактическое открытие двери этим тестом не проверено. Дополнительная попытка reflection-теста конструктора MemoryPersistence не годится для R8, изменяющего вспомогательные конструкторы; итоговый smoke проверяет именно библиотечную фабрику, вызвавшую исходный сбой.
PASS: release app-v1.0.292 опубликован, публичные APK/JSON перечитаны без авторизации и сверены publisher; SHA-256 fce40cc8584254e8a28981199ed6257b0c4610a8234433f3aa153c387b2d0b96. Сертификат прежний be9a249131a80bfe499c35dbad0752a13be5c4d02e4198b9da436e30d59885c0. APP 290 удалён; ESP 453 остаётся global latest. APK/metadata/publication: production/android-release-292; свидетельства до/после и ориентация: .device-agent/mqtt-release-smoke/ и mqtt-fix-292-orientation.json. Приложение обновлено поверх release без сброса настроек.


## ESP 10.0.455 — проверка нового Web UI, 10.10.2026
PASS: исходная 454 и итоговая 455 собраны build-esp32.ps1 -Profile finalize-existing; RSA-подпись и encrypted OTA round-trip успешны. Подписанный образ 1314816 байт, свободно 33% OTA-раздела; encrypted OTA SHA-256 a9b0594229d1369fbeee13bc16e8418391f8d113ae8c79361f0a2796deb462fa.
PASS: Node JavaScript syntax; реальный JS в VM проверил формат uptime на границах секунд/минут/часов/дней и сброс ошибки, load/save MQTT без удалённого mEnabled, постоянный enabled=true и trim домена, busy-блокировку и ошибку API. Повторно пройдена существующая проверка slider/RSSI API, timer_a, звука, диагностики, testOpen и DOM OTA/регистрации (все API mocked; физическое открытие не запускалось). ID уникальны; HTML совпадает с исходным макетом. PASS: мобильный preview через Edge/CDP с viewport=390, scrollWidth=390 (без горизонтального переполнения), screenshot просмотрен; кнопка OTA переключена существующим useInternalFirmware. Это preview с фиктивными API-данными, не экран текущего замка. Артефакты production/esp-10.0.455 и production/verification/web-ui-455. Публикация проверяется отдельно; реальную установку пользователь выполняет самостоятельно с телефона.

Публикация 455 завершена. Первое чтение global latest сразу после публикации вернуло старый кеш, поэтому штатный publisher остановил удаление предыдущего релиза на несовпадении SHA. Повторное чтение публичного latest с Cache-Control:no-cache и прямого tagged URL вернуло правильный SHA-256 a9b0594229d1369fbeee13bc16e8418391f8d113ae8c79361f0a2796deb462fa; после этого штатная prune_old_releases удалила только ESP 453 и его тег. API подтвердил ровно esp-v10.0.455 и app-v1.0.292. Encrypted OTA 1315328 байт. Реальная OTA установка 455 на замок не выполнялась: пользователь устанавливает с телефона самостоятельно.


## Android 294 — проверка скрытого состояния, 10.10.2026
PASS: baseline release 293 и итоговая release 294 проектным build-android.ps1 -Action release, включая R8/lintVital. APK/версия/сертификат проверены verify_apk, 294 установлен поверх 292 по Wi-Fi 192.168.1.65:33719, ADB Success. accelerometer_rotation/user_rotation сохранены, восстановлены и проверены 0/0. UIAutomator подтвердил сборку 294, отсутствие кнопки добавления виджета, отсутствие ручной проверки и отсутствие строки обновления при установленной более новой версии относительно GitHub 292. Для проверки появления активной строки готовится следующий выпуск с тем же кодом.


## Android 295 — проверка появления строки, 10.10.2026
PASS: release 295 собрана и опубликована; APK/JSON/подпись проверены publisher. На телефоне 294 новое приложение обнаружено после HOME→открытие главного экрана, без ручной проверки; появилась ровно строка «Есть обновление». Нажатие передало проверенный APK Android без диалога описания приложения. Найдено ограничение ACTION_VIEW: на текущем телефоне появляется chooser с Package Installer и WPS Office. Следующий этап направит Intent явно в системный APK handler. accelerometer_rotation/user_rotation сохранены и проверены 0/0; установка 295 этим нажатием ещё не подтверждена.


## Android 296 — исправление chooser, 10.10.2026
PASS: release 296 собрана проектным скриптом с R8/lintVital; APK/версия/подпись проверены verify_apk, установлен поверх 294 ADB Success. Ориентация сохранена, восстановлена и проверена 0/0. Запрос APK handlers на телефоне показывает com.google.android.packageinstaller и WPS Office; новая логика выбирает только FLAG_SYSTEM. Для проверки фактического перехода без chooser готовится следующий выпуск с тем же кодом.


## Android 297 — итоговая проверка обновления, 10.10.2026
PASS: release 297 успешно собрана build-android.ps1 -Action release, включая R8/lintVital; APK и metadata проверены и опубликованы как app-v1.0.297. SHA-256 APK: fdd4f449599f7e6d0cf73d05b0d3538539c02b60e7d6c2846b1e2f0766e5fc83. Предыдущий APP 295 удалён publisher; ESP 455 сохранён. На телефоне обновление из приложения дошло до системной установки и проверки безопасности MIUI; экран проверки показал 1.0.297. dumpsys package подтвердил versionCode=297/versionName=1.0.297. UIAutomator после установки подтвердил «Сборка 297», отсутствие кнопки добавления виджета, ручной проверки и строки «Есть обновление» для актуальной версии. Появление строки для новой версии и её нажатие проверены ранее на 294→295. accelerometer_rotation/user_rotation повторно прочитаны: 0/0. Свидетельства: .device-agent/update-line-297-installer.xml и update-line-297-installed.xml.


## Android 299 — проверка компактного виджета, 10.10.2026
PASS: baseline release 298 до изменений и итоговая release 299 успешно собраны проектным скриптом, включая R8/lintVital. verify_apk подтвердил пакет, номер и постоянную подпись. Установка поверх 297 по Wi-Fi 192.168.1.65:33719: Success; dumpsys package подтвердил versionCode=299/versionName=1.0.299. До установки сохранены accelerometer_rotation/user_rotation, после восстановлены и проверены 0/0 (.device-agent/widget-299-orientation.json). dumpsys appwidget подтвердил обновлённый provider min=(10241x10241), соответствующий 40 dp вместо прежних 60 dp, и сохранённый экземпляр виджета id=78. Визуальное размещение на сетке не проверено: телефон показывает заблокированный системный экран NotificationShade. Уже выделенная площадь старого экземпляра управляется launcher; при её сохранении требуется повторное добавление. Нажатие виджета не выполнялось, чтобы не переключать режим замка при проверке размеров. Публикация app-v1.0.299 успешна, SHA-256 22b98cbb0c7b812e4286fd2ef153acedcb8b78b4ae15932f344f71e1a516a684; прежний APP 297 удалён, ESP 455 сохранён.


## Android 301 — фактическая сетка launcher, 10.10.2026
PASS: baseline release 300 и итоговая release 301 успешно собраны build-android.ps1 -Action release (R8/lintVital). APK проверен verify_apk и установлен поверх 299 по Wi-Fi, Success, versionCode=301/versionName=1.0.301 подтверждены. Ориентация до установки сохранена, после восстановлена и повторно проверена 0/0 (.device-agent/widget-301-orientation.json).
На разблокированном телефоне исходная область widget_button [64,1290][508,1796] = 444×506 px (2×2), значок 135×135 px. После 301 доступно штатное «Налаштувати розмір». Существующий экземпляр сжат по ширине и высоте до [64,1290][254,1521] = 190×231 px (1×1). Значок [74,1321][243,1490] = 169×169 px, соответствует размеру обычного значка приложения на этом экране. Повторное длительное нажатие подтвердило сохранение области 1×1; скриншот просмотрен. Режим автооткрытия не переключался, другие значки не переносились. Свидетельства .device-agent/widget-grid-before.*, widget-grid-301-resize.*, widget-grid-301-one-cell.*, widget-grid-301-longpress-final.*.
Публикация app-v1.0.301 успешна: SHA-256 adda5a4633cd3929ceb41855148dc983253a80f291075ebecc41c9d5a6e92720, APP 299 удалён, ESP 455 сохранён. Начальный span нового экземпляра отдельно не проверен; существующая сохранённая область корректируется launcher, не приложением.


## ESP 457 — единый путь OTA, 10.10.2026
PASS: baseline ESP 456 и итоговая 457 успешно собраны build-esp32.ps1 -Profile finalize-existing, RSA-подпись и encrypted OTA round-trip проверены. Образ 1314816 байт, свободно 33% OTA-раздела; encrypted OTA 1315328 байт, SHA-256 11d89b33e34d4abd7995b4ccb5fe400127160a9c6cdb51032a7a0aa11b253238. Дополнительная Android baseline release 302 успешно собрана; Android-код не изменён, APK 302 не устанавливался и не публиковался, актуальный APP остаётся 301.
PASS: реальный JS Web UI в Node/VM со штатным Android firmware_transfer.js: статический текст/отсутствие picker, запрет передачи вне приложения, получение кешированного образа, вызов started(version) до POST /api/ota, успешная смена версии и запись завершения, ошибки кеша/bridge/OTA с разблокировкой кнопки и очисткой состояния, блокировка повторного нажатия. Моки моделируют сеть и bridge; физический OTA в этих проверках не выполняется. Ошибка первоначального тестового mock location.origin исправлена без изменения кода продукта. Проверка .device-agent/test-web-ota-457.cjs, копия production/verification/web-ota-457/.
Публикация esp-v10.0.457 завершена. Первое чтение latest вернуло кеш старого образа и штатный publisher остановил очистку. Повторно публичный latest и tagged download проверены без кеша: оба SHA совпадают. После проверки удалён только ESP 455; API подтвердил ESP 457 и APP 301. Комплект production/esp-10.0.457/, public-release.json в verification/web-ota-457/. Реальная установка на замок не выполнялась: пользователь устанавливает с телефона самостоятельно.


## ESP 459 — проверка двух этапов калибровки, 10.10.2026
PASS: baseline 458 и итоговая 459 успешно собраны build-esp32.ps1 -Profile finalize-existing; RSA-подпись и encrypted OTA round-trip проверены. Подписанный образ 1314816 байт, свободно 33%; encrypted OTA 1315328 байт, SHA-256 533e867961188cb622e8bbafbcf59c4983c05751a125dffe917965c407ffc438.
PASS: реальный JS мастера в Node/VM прошёл start→20 измерений закрытия→автоматический verify→20 измерений открытия→save(closed,open,verify)→завершение. Проверены два завершённых индикатора без s2, отсутствие бумаги, равные точки/слишком малая разница и ошибка сохранения. Минимальный положительный диапазон 7 ADC соответствует прежней проверке. tools/test_wizard_speech.cjs прошёл: ожидание озвучки, повтор, устаревшие callbacks/timers, ошибка TTS, истечение и завершение. Моки моделируют замок; физическая калибровка и запись NVS замка не запускались. Свидетельства production/verification/wizard-two-459/test-wizard-two-459.cjs.
Публикация esp-v10.0.459 успешна. Первичный latest снова вернул кеш предыдущего образа; publisher остановил удаление старого релиза. Публичный latest и tagged URL затем проверены без кеша, SHA совпали, удалён только ESP 457. API подтвердил ESP 459 и APP 301. Комплект production/esp-10.0.459/, public-release.json в verification/wizard-two-459/. Реальная установка не выполнялась: пользователь устанавливает с телефона самостоятельно.


## ESP 461 — проверка переноса кнопки, 10.10.2026
PASS: baseline 460 и итоговая 461 успешно собраны build-esp32.ps1 -Profile finalize-existing; подпись RSA и encrypted OTA round-trip проверены. Проверка исходного HTML подтвердила одну кнопку мастера в CFG, первой строкой после заголовка и до pr, отсутствие кнопки в прежнем блоке и исходный handler window.open(/calibration). git diff --check успешен. Изменение только размещения, дополнительные тесты калибровки не запускались. Подписанный образ 1314816 байт, 33% OTA-раздела свободно; encrypted OTA 1315328 байт, SHA-256 fa1892aa6e714062321e59bf90377a8d36b386df2de5de596b147304c4f1d7b0. Публикация esp-v10.0.461 прошла, публичный latest SHA проверен штатным publisher; удалён ESP 459, APP 301 не затронут. Комплект production/esp-10.0.461/, подтверждение verification/wizard-placement-461/public-release.json. Реальную установку пользователь выполняет с телефона самостоятельно; она не выполнялась агентом.


## Android 304 — проверка запуска обновлений ESP, 10.10.2026
PASS: исходная release 303 и итоговая release 304 успешно собраны build-android.ps1 -Action release, включая R8/lintVital. build-android.ps1 -Action test успешен: debug 62/62 и release 62/62, без failures/errors/skips, включая FirmwareUpdateClient/FirmwareTransferState. Проверка исходного onResume подтвердила Handler.post без задержки; running guard не создаёт повторный запрос, completion удаляет лишние callbacks.
APK проверен verify_apk и установлен поверх 301 по Wi-Fi 192.168.1.65:33719, Success, versionCode=304/versionName=1.0.304 подтверждены. accelerometer_rotation/user_rotation сохранены перед установкой, восстановлены и проверены 0/0 (.device-agent/firmware-check-304-orientation.json). Главный экран после установки и повторного HOME→открытия отображает сборку 304 и пометку «(доступно обновление)» на настройках замка, UIAutomator .device-agent/firmware-check-304-open.xml. Из этого UI-снимка нельзя отдельно определить момент HTTP-запроса или номер уже сохранённого образа; немедленный запуск подтверждён кодом lifecycle. Команды открытия/OTA на замок не отправлялись.
Публикация app-v1.0.304 успешна: SHA-256 b5b21b185e965572dafaf6f597232e40dade1a21979415531578d69bc99ee830; предыдущий APP 301 удалён, ESP 461 сохранён. Комплект production/android-release-304/.


## ESP 463 — проверка названия мастера, 10.10.2026
PASS: baseline 462 и итоговая 463 успешно собраны build-esp32.ps1 -Profile finalize-existing. RSA-подпись и encrypted OTA round-trip успешны. rg main и Android app/src/main подтвердил три новых названия и отсутствие прежнего. git diff --check успешен. Для текстового изменения дополнительные тесты не создавались. Подписанный образ 1314816 байт, свободно 33%; encrypted OTA 1315328 байт, SHA-256 14fc38ced81df8a16e663e9dfe7204514a4dee3d688c3389c71dcc1aef6f1d91. Публикация esp-v10.0.463 успешна, публичный latest SHA проверен publisher; ESP 461 удалён, APP 304 сохранён. Комплект production/esp-10.0.463/, public-release.json в verification/wizard-name-463/. Физическую установку пользователь выполняет с телефона самостоятельно, агент не выполнял.


## ESP 466 — проверки регулятора сигнала, 10.10.2026
PASS: исходная сборка 464 до изменений, промежуточная 465 и итоговая 466 успешно собраны build-esp32.ps1 -Profile finalize-existing. RSA-подпись и encrypted OTA round-trip успешны. Подписанный образ 1380352 байт, 30% OTA-раздела свободно; encrypted OTA 1380864 байт, SHA-256 dab8cd8404c40b401b7472aa81eab96267b6b4d1c22c1385f6797eb66bab699c.
PASS: 9 сценариев прежнего NVS default-флага и 15 сценариев длительности/API: начальное сохранение, повторный запуск, сохранённое значение, исправление некорректного значения, границы/шаг, ошибки чтения/открытия/записи/commit, отсутствие изменения RAM при ошибке, HTTP 400/500. Тесты исполняют извлечённые функции проекта в RISC-V/Unicorn; это не испытание на физическом ESP.
PASS: JavaScript реального HTML — показ мс при движении без POST, загрузка сохранённого значения, сохранение при отпускании, блокировка повторного запроса, откат при ошибке, повторная загрузка. Ползунок использует существующий полноширинный класс zone-slider.
Физическая установка и оценка звука не выполнялись: пользователь устанавливает прошивку с телефона самостоятельно. Другие длительности сигналов и алгоритм открытия не изменены.

Публикация esp-v10.0.466 успешна; SHA-256 публичных latest и tagged загрузок совпадает. Предыдущий ESP 463 удалён, APP 304 сохранён. Комплект production/esp-10.0.466/, подтверждение production/verification/departure-pulse-466/public-release.json.


## ESP 468 — ритм и точность длительности, 10.10.2026
PASS: baseline 467 до правок и итоговая 468 успешно собраны проектным build-esp32.ps1 -Profile finalize-existing; подпись RSA и encrypted OTA round-trip успешны. Подписанный образ 1380352 байт, свободно 30%; encrypted OTA 1380864 байт, SHA-256 9c6101a3b319b736e1d974086fe9c858c4ffd0f012c62b6bc372efa635645ce0.
PASS: 9 прежних сценариев NVS и 19 обновлённых сценариев config/API/таймера. Исполнены извлечённые настоящие функции в RISC-V/Unicorn с моделью таймера и планировщика: все 156 длительностей 5–160 мс устанавливают timeout width*1000 мкс, имеют одинаковый период 18 ticks и выключают GPIO; прерывание новым overlap останавливает таймер/выключает сигнал; ошибки запуска и отсутствующий таймер обработаны. Валидны границы 5/160 и значение 61, недопустимые значения отклонены, прежний NVS за пределами диапазона заменяется 20 мс.
PASS: реальный JavaScript HTML — диапазон 5–160/1, мгновенный показ 81 мс без POST при движении, сохранение после отпускания, busy guard, откат при ошибке, повторная загрузка. git diff --check успешен.
180 мс — расчёт по старому коду 20 мс + восемь slices по 25 мс при FreeRTOS 100 Hz, без накладных расходов; измерение осциллографом не выполнялось. esp_timer работает в своей задаче, точность зависит от её планирования. Установка на замок и физическая оценка звука не выполняются агентом: пользователь устанавливает с телефона самостоятельно.

ESP 468 опубликован; SHA-256 публичных latest и tagged загрузок проверен. Предыдущий ESP 466 удалён, APP 304 сохранён. Установку пользователь выполняет самостоятельно.


## ESP 471 — проверки предпрослушивания, 10.10.2026
PASS: baseline 469 до изменений и итоговая 471 успешно собраны build-esp32.ps1 -Profile finalize-existing. Попытка 470 выявила пропущенный include app_diagnostics.h в web_ui.c; после его добавления повторная сборка 471 успешна, дальнейшие изменения продукта не выполнялись. RSA-подпись и encrypted OTA round-trip успешны. Подписанный образ 1380352 байт, свободно 30%; encrypted OTA 1380864 байт, SHA-256 f0c683c88b44ff9a67df5952f849d2826450ea645ed22d6aa9e2ef6031812e17.
PASS: tools/test-departure-pulse.py — 23 сценария настоящих извлечённых функций NVS/API/таймера в RISC-V/Unicorn. В дополнение к 19 прежним сценариям проверены deadline ровно пять секунд, обрезка последнего импульса по оставшемуся времени, истечение, повторное движение за 100 мс до истечения с новой длительностью и новым deadline, недопустимые аргументы/неготовый таймер, HTTP 400/500/успех, отсутствие записи NVS во время предпрослушивания. Состояние RAM защищено portMUX; сам spinlock в эмуляции заменён stub, многопроцессорный стресс-тест не проводился.
PASS: JavaScript реального HTML — input запускает preview и сразу показывает мс; только один preview-запрос одновременно; быстрые движения объединяются до последнего; сохранение после отпускания, загрузка/откат не запускают preview; ошибки показываются и последующее движение работает. Подмена HTTP в тесте проверяет отделение preview от записи NVS; реальные BLE/HTTP задержки не измерялись. Новый route использует существующий PHONE_ONLY_HANDLER и укладывается в max_uri_handlers=40.
Установку пользователь выполняет с телефона самостоятельно. Звук на физическом устройстве не проверялся; агент OTA не выполнял.

ESP 471 опубликован; публичные latest/tagged SHA-256 совпадают с итоговой сборкой. ESP 468 удалён, APP 304 сохранён. Комплект production/esp-10.0.471/, подтверждение production/verification/departure-preview-471/public-release.json.


## ESP 473 — проверка ползунка задержки, 10.10.2026
PASS: исходная 472 до изменений и итоговая 473 успешно собраны build-esp32.ps1 -Profile finalize-existing. RSA-подпись и encrypted OTA round-trip успешны. Подписанный образ 1380352 байт, 30% OTA-раздела свободно; encrypted OTA 1380864 байт, SHA-256 682fb82faa501655b992efc80464bca7f943f810058adaa6cbe88d99f847a624.
PASS: одноразовая Node/VM проверка реального JavaScript — type=range, границы 3000/10000, шаг 1; показ 3000, 5678 и 10000 мс; движение не делает HTTP-запросов/не запускает звук; saveConfig передаёт timer_a_ms=10000 и прежний phone_rssi_threshold=-68. Существующие проверки предпрослушивания звука прошли. git diff --check успешен. Постоянные новые тесты для изменения представления не добавлялись.
Значения NVS вне диапазона при загрузке ограничиваются браузером в ползунке; до нажатия «Сохранить» NVS не переписывается. Алгоритм и реальные интервалы закрытия не изменены. Физическая установка не выполнялась: пользователь устанавливает с телефона самостоятельно.

ESP 473 опубликован, SHA-256 публичных latest и tagged загрузок совпадает с итоговой сборкой. ESP 471 удалён, APP 304 сохранён. Комплект production/esp-10.0.473/, подтверждение verification/delay-slider-473/public-release.json.


## ESP 475 — проверка десятых секунды, 10.10.2026
PASS: baseline 474 до изменений и итоговая 475 собраны build-esp32.ps1 -Profile finalize-existing. Подпись RSA и encrypted OTA round-trip успешны. Подписанный образ 1380352 байт, свободно 30%; encrypted 1380864 байт, SHA-256 321f559e409768c68e6400568741b8997119cf6cc626f9765e70ba069981dceb.
PASS: одноразовая проверка реального JavaScript в Node/VM — step=100, границы 3000/10000, показ 3,0/5,7/10,0 с, движение не отправляет запрос, сохранение передаёт timer_a_ms=10000. Проверки preview звука прошли; git diff --check успешен. При подготовке проверки исправлена слишком широкая замена шага в самом временном тесте; продукт не менялся. Постоянные тесты для изменения представления не добавлялись.
Физическая установка не выполнялась, пользователь устанавливает с телефона самостоятельно.

ESP 475 опубликован. Первоначальный publisher завершился с ненулевым кодом после загрузки; отдельная проверка подтвердила публичные latest/tagged SHA-256, затем удалён ESP 473. APP 304 сохранён. Комплект production/esp-10.0.475/, подтверждение verification/delay-slider-475/public-release.json.


## ESP 478 — проверка списка брокеров, 10.10.2026
PASS: baseline 476 до правок, итоговая 478 успешно собраны build-esp32.ps1 -Profile finalize-existing. Попытка 477 обнаружила кодировку UTF-16 исходного файла; добавлена обработка BOM, повторная сборка успешна. RSA-подпись и encrypted OTA round-trip успешны; подписанный образ 1380352 байт, 30% OTA-раздела свободно; encrypted 1380864 байт, SHA-256 7fb1ea1b13eb253e71428740ed82e36cf19d62cd391c430780a9383e3586b0aa.
Реальная проверка в сборке: 12 строк / 10 уникальных адресов, все вернули CONNACK Success без username/password. Проверялось соединение MQTT 3.1.1:1883, без SUBSCRIBE/PUBLISH. Полный отчёт production/verification/mqtt-choices-478/mqtt_brokers_report.json; source_sha256 совпадает с файлом. JSON принятого списка найден в скомпилированном unsigned образе.
PASS: tools/test_mqtt_brokers.py — 5 локальных тестов через настоящий Paho: успешный CONNACK, требование авторизации (отказ), TCP без CONNACK (отказ), дубликаты/невалидные адреса, UTF-16 и остановка CLI при нуле успешных адресов с сохранением прежнего header. Ошибка target блокирует выпуск новой прошивки, старый header не служит обходом проверки.
PASS: реальный JavaScript HTML в Node/VM — заполнение datalist из API; сохранённый адрес вне списка остаётся в поле; выбор из списка и ручной ввод сохраняют host существующим API; broker_choices отсутствует в POST и не сохраняется NVS. Проверки задержки/preview звука успешны; git diff --check успешен. Многопроцессорное поведение ESP и визуальное раскрытие datalist на физическом телефоне отдельно не проверены.
Android не менялся. Физическую установку пользователь выполняет самостоятельно, агент OTA не выполнял. Успех CONNACK не подтверждает ACL топиков и дальнейшую доступность брокера.

ESP 478 опубликован; публичные latest/tagged SHA-256 совпадают с итоговой сборкой. ESP 475 удалён, APP 304 сохранён. Комплект production/esp-10.0.478/, подтверждение verification/mqtt-choices-478/public-release.json.


## ESP 480 — проверка раскрытия брокеров, 10.10.2026
PASS: baseline 479 до изменений, итоговая 480 успешно собраны build-esp32.ps1 -Profile finalize-existing. Проверка MQTT повторена при обеих сборках. Подпись RSA и encrypted OTA round-trip успешны. Подписанный образ 1380352 байт, 30% OTA-раздела свободно; encrypted 1380864 байт, SHA-256 390d28bf281446cce067e8efeea45bd17f10932210792b6d4f77b7b414b84f0a.
PASS: реальный HTML/JavaScript в headless Edge через CDP на ширине 390 px: стрелка открывает все адреса при уже заполненном поле broker.emqx.io, aria-expanded соответствует состоянию; пункты >=44 px, нет горизонтального переполнения; выбор последнего пункта меняет поле и закрывает список без сохранения; выбор и ручной адрес сохраняются прежним API. Проверены закрытие при вводе, Escape, наружном клике и повторной стрелке, пустой список не блокирует ручной ввод. Снимок визуально просмотрен: production/verification/mqtt-picker-480/mobile.png. HTTP ответы в браузерной проверке подменены, команды на замок не отправлялись; проверка физического Android WebView не выполнялась.
Установка на замок остаётся за пользователем, агент OTA не выполнял. Android менять не требуется.

ESP 480 опубликован; публичные latest/tagged SHA-256 совпадают с итоговой сборкой. ESP 478 удалён, APP 304 сохранён. Проверки: verification/mqtt-picker-480/, git diff --check успешен.


## Android 306 — проверка подписи брокера, 10.10.2026
PASS: исходная release 305 до правок и итоговая release 306 успешно собраны build-android.ps1 -Action release, включая R8 и lintVital. Источник host в кнопке и InternetOpen.start сверены: оба читают InternetAccess.load(). Статическое чтение подтвердило listener изменения sealed, attach/detach и отсутствие изменения стадий. Дополнительные тесты для замены подписи не создавались; git diff --check успешен.
APK проверен verify_apk, подписан постоянным release certificate SHA-256 be9a249131a80bfe499c35dbad0752a13be5c4d02e4198b9da436e30d59885c0. Установка по Wi-Fi 192.168.1.65:33719 успешна, dumpsys подтвердил versionCode=306/versionName=1.0.306. accelerometer_rotation/user_rotation сохранены перед установкой и восстановлены с повторной проверкой 0/0; .device-agent/broker-label-306-orientation.json.
Телефон заблокирован: после запуска и пробуждения UIAutomator показывает lock screen, визуальный вид надписи на физическом устройстве не подтверждён. Интернет-команды открытия не отправлялись, настройки MQTT не менялись. ESP не изменялся.

APP 306 опубликован: SHA-256 eea3422a5f020c48c63d43bfecd1b5ba23a4f7402183530dd27b2ef67221a34c. Публичные APK/metadata проверены publisher, APP 304 удалён, ESP global latest сохранён. Комплект production/android-release-306/.

Проверка пользователем: 10.10.2026 пользователь подтвердил, что отображение текущего брокера в кнопке интернет-открытия работает. Приложение 306 и прошивка 480 подготовлены к отдельным коммитам; код после успешных сборок не изменялся.


## Проверка постоянной ссылки Android — 10.10.2026
PASS: Node/VM реального JavaScript — выбор APP 306 при наличии ESP 480/старого APP/нового draft, игнорирование draft, переход на ожидаемый APK, сообщение при HTTP ошибке, отклонение постороннего URL. Публичный коммит 7ad1e3b отправлен без локальной истории исходников. ESP/Android не менялись, повторные сборки не требуются для описания и отдельной страницы загрузки.

PASS: публичный README совпадает с подготовленным текстом; https://uah0.github.io/thekidlock/download/ доступен и содержит актуальный обработчик. Live GitHub API разрешает CORS и выбирает APK app-v1.0.306. Подтверждение .device-agent/public-readme-verification.json.
## Android 308 — проверка защиты приложения, 10.10.2026

PASS: baseline 307 до изменений и итоговая 308 собраны проектным build-android.ps1 -Action release.
PASS: build-android.ps1 -Action test — 68 debug + 68 release тестов, ошибок/пропусков нет.
FirmwareUpdateClient: запрещённый redirect не вызывает подключения к чужому host (GET/HEAD),
HTTP downgrade, userInfo, порт и чужой репозиторий отвергаются; разрешённый signed URL assets
работает; цикл ограничен шестью запросами; ошибки/нет Location закрывают соединение;
loopback разрешён только debug и не допускает redirect. Проверка очистки буферов — чтением
finally путей, ошибки Android Keystore на физическом устройстве не моделировались.

PASS: фактический release APK проверен apksigner, aapt/aapt2 и dexdump.
applicationId com.blelock.app, version 1.0.308, debuggable=false, прежний сертификат подписи.
Размер 472768 байт, SHA-256 2e3833f0527471d3c07c86f58c3629f4b05210472a9340f109cbf3c14a890d63.
Новые backup правила действительно упакованы: 9 исключений Android 8–11 и по 9 для cloud/D2D.
Обфускация/оптимизация классов доступа и OTA подтверждена mapping и DEX; Android Log вызовов 0.
Файлов закрытых ключей/подписи и PEM private-key маркеров нет. Дополнительные exported
компоненты AndroidX защищены системными BIND_JOB_SERVICE/DUMP. git diff --check успешен.
Доказательства .device-agent/android-security-308.json; локальный APK production/android-release-308/thekidlock.apk.
НЕ ПРОВЕРЯЛОСЬ: реальный D2D, root/hooking, враждебная локальная сеть, BLE на физическом замке.
APK не установлен и не опубликован. ESP не изменялась; пользовательские удаления не затронуты.
## Android 310 — многоязычность, этап 1, 10.10.2026

PASS: baseline release 309 до правок; release 310 успешно собрана; 71 debug и 71 release
unit-тестов без ошибок. Проверены все ключи en/ru/uk, длины arrays, positional placeholders,
отсутствие кириллицы в English fallback, объявление трёх языков и accessibility resource IDs.
ADB 192.168.1.65:33719 после reconnect доступен; телефон показывает keyguard, UI пока не проверен.
## Android 311 — многоязычность, итог, 10.10.2026

PASS: release 311 успешно собрана проектным скриптом, R8/lintVital успешны.
PASS: повторные 71 debug + 71 release тестов без failures/errors, включая три новых
LocalizationResourcesTest и обновлённый accessibility тест. Полные 176 resource элементов
в четырёх source sets, одинаковые keys/array lengths/форматы, English без кириллицы.
PASS: aapt/aapt2 фактического APK подтверждают manifest localeConfig en/ru/uk и полный
набор default/en/ru/uk для 13 действующих текстов: главные кнопки, пять MQTT состояний,
уведомления, ошибка обновления и widget accessibility. Неиспользуемые ресурсы сокращает R8.
PASS: apksigner/DEX/backup проверки — прежняя подпись, debuggable=false, Android Log invoke=0,
9 старых backup и 18 cloud/D2D исключений. Размер 534752 байт, SHA-256
9f75a45457b49df421e3b9734040496b47aaa03e5e506ab6bc5f5f430e3eee5a.
PASS: git diff --check; сравнения строк протокола сохранены, обработчик языка не включает
BLE и не меняет autoOpenEnabled. Изменены только файлы локализации, список в CHANGELOG.md.

НЕ ВЫПОЛНЕНО: установка и визуальная проверка на телефоне. Два reconnect 192.168.1.65:33719
сообщили connected, но shell чтение accelerometer_rotation завершилось ошибкой.
Отказ произошёл до install; ориентация/язык не менялись. Эмулятор не создавался.
Доказательства .device-agent/localization-apk-311.json, android-security-311.json;
локальная проверенная копия production/android-release-311/thekidlock.apk. GitHub/ESP не изменены.
## Android 312 — окончательная проверка и установка, 10.10.2026

PASS: build-android.ps1 -Action release и -Action test, 71 debug + 71 release тестов.
177 resource элементов в default/en/ru/uk, keys/array sizes/format placeholders согласованы;
кнопки, пять интернет-состояний и другие 13 активных строк сверены в фактическом APK.
PASS: подпись прежняя, debuggable=false, R8/backup/Log checks сохранены. Размер 535264,
SHA-256 3712c8a8d393ba264996b3c9050f2b1f2929972ad938e651b129af4837e66171.
PASS: после восстановления ADB APK установлен через 192.168.1.65:33719, versionCode=312,
versionName=1.0.312 подтверждены. На устройстве cmd locale переключён en/uk/ru и подтвердил
каждый tag; английское notification показывает “TheKidLock is running in the background”
и “Service BLE exchange with lock”. Для uk/ru активного foreground notification не было:
его текст не подтверждён динамически, ресурсы проверены. App locale [] восстановлен,
accelerometer_rotation/user_rotation сохранены до install и после восстановления перечитаны 0/0.
НЕ ПРОВЕРЕНО: визуальный вид/размеры переводов, телефон locked=true. Команды открытия
и смены режимов не отправлялись. GitHub/ESP не изменены; git diff --check успешен.
Доказательства .device-agent/localization-install-312.json, localization-apk-312.json,
android-security-312.json. Комплект production/android-release-312/.

## Android — экран параметров приложения и выбор языка, 10.10.2026

До изменений базовая release-сборка 314: PASS, проектный build-android.ps1 -Action release.

После изменений: проверка сборкой release 315 ожидается. Дополнительные тесты не запускались.
Ручная проверка экрана и переключения на физическом телефоне не выполнялась.
Ориентация экрана телефона не менялась, APK через ADB не устанавливался. GitHub и ESP не изменялись.

Итог проверки экрана меню: release 316 собран успешно через build-android.ps1 -Action release. APK 540188 байт, SHA-256 F1164E47187A474DB132CFACEF745B54B88306930D9B2EC2FFE892151547D54A. git diff --check прошёл. Unit tests и установка на телефон для этой задачи не выполнялись; ориентация не менялась. GitHub и ESP не затронуты.

Дополнение после вопроса пользователя: release 316 установлен поверх приложения на телефон через ADB Wi-Fi 192.168.1.65:33719. Первая попытка оборвалась по device offline; после переподключения установка прошла. На устройстве подтверждены versionCode=316/versionName=1.0.316. Перед повторной установкой сохранены accelerometer_rotation/user_rotation=0/0; после установки оба значения восстановлены и повторно прочитаны как 0/0. Приложение вручную не запускалось, визуальную проверку нового экрана не выполнял.

## Android — значки кнопки настроек замка, 10.10.2026

Базовая release-сборка перед изменением: PASS, build-android.ps1 -Action release, версия 1.0.317. Итоговая сборка и установка будут отмечены после проверки.

Итог иконок кнопки настроек: Android release 1.0.318 собран успешно. APK установлен на телефон через ADB Wi-Fi 192.168.1.65:33719; package dump подтвердил versionCode/versionName 318/1.0.318. accelerometer_rotation/user_rotation были сохранены как 0/0, восстановлены и повторно подтверждены как 0/0. Интерфейс не запускал вручную, чтобы не инициировать BLE-сеанс; компиляция и установка проверены, визуальная проверка на экране не выполнялась.git diff --check ожидается.

## Web UI localization baseline — 10.10.2026

PASS: build-android.ps1 -Action release до изменений. PASS: build-esp32.ps1 -Action build -Profile signed-dev, ESP-IDF 5.4.4 до изменений; firmare build 482, encrypted OTA подготовлена и проверена. На устройство baseline не загружался. Проверка финальных изменений ожидается.

## ESP Web UI localization baseline

PASS: build-esp32.ps1 -Action build -Profile signed-dev, ESP-IDF 5.4.4; binary signed, encrypted OTA prepared; 33% app partition free. Финальная проверка будет добавлена после реализации. Не прошивалось.

Итог Web UI localization: PASS Android release build 1.0.320; PASS ESP-IDF 5.4.4 signed-dev build 483, firmware подписана и encrypted OTA подготовлена, размер 0x141000, app partition свободно 33%. PASS JSON validation: en/ru/uk по 125 одинаковым ключам, placeholders согласованы, ключи разметки обеих HTML-страниц присутствуют. PASS git diff --check. APK 1.0.320 установлен по ADB Wi-Fi 192.168.1.65:33719 без запуска приложения; перед установкой accelerometer_rotation/user_rotation=0/0, после установки повторно проверены 0/0. OTA проверка блокирована недоступностью сохранённого адреса замка 192.168.1.54 и отсутствием результата автообнаружения в ARP-кэше; firmware_version нового образа на замке не подтверждена. GitHub не изменялся.

Дополнение 11.10.2026: после уточнения пользователя использован штатный путь через установленное приложение и встроенный Web UI по Wi-Fi. В Web UI замка отображается версия 10.0.483 и время сборки 2026-10-10 23:42:32 (снимок .device-agent/fw-root-top.png); обновление на ESP подтверждено. Релиз esp-v10.0.483 опубликован и доступен по latest URL, прежний ESP-релиз 480 удалён согласно политике одного актуального релиза. При переходе Web UI открылся мастер калибровки; он был отменён, значения калибровки не сохранялись.

## Симулятор замка — Android 323

PASS: Android debug/release unit tests, 71 + 71; debug APK build; release APK build
1.0.323 (548305 байт, SHA-256
FC660BAC7BBC3CF19CFDC2A7382288E34DA4E51F6D041A625161AB98F3921207).
PASS: release APK установлен поверх 1.0.320 на 192.168.1.65:33719; package dump
подтвердил versionCode/versionName 323/1.0.323. До установки и после неё
accelerometer_rotation/user_rotation прочитаны как 0/0. APK установлен с прежней
подписью приложения; попытка обновить release debug APK отклонена Android из-за
различия подписей, приложение не удалялось.
Физический экран заблокирован, поэтому ручная проверка пунктов меню и пяти статусов
не выполнена. Firmware build 484 успешно передан через encrypted OTA;
device-agent подтвердил смену 10.0.483 → 10.0.484.

## Android 325 — виртуальный замок

PASS: `build-android.ps1 -Action test` — 71 debug + 71 release unit tests.
PASS: `build-android.ps1 -Action release`; APK 1.0.325, 550233 байта,
SHA-256 `DC03A1E73E82C8A962FC9251F8AF5F0212FDCC0BCE04C05E74C7164BD0C8153E`.
PASS: APK установлен по Wi-Fi ADB, versionCode/versionName 325/1.0.325 подтверждены.
До и после установки `accelerometer_rotation/user_rotation` равны 0/0.
Ограничение: после старта приложения ADB стало offline, поэтому переключение режима
и визуальное поведение на устройстве вручную не проверены.

## Отмена симулятора — Android 327, 11.10.2026

PASS: обязательная исходная debug-сборка до изменения кода (326).
PASS: финальная debug-сборка проектным build-android.ps1 (327).
PASS: app/src/main совпадает с b179041; изменены только файлы реализации симулятора,
BUILD_NUMBER и документация. ESP не изменялся. APK на телефон не устанавливался.
Демонстрационный сценарий не реализован и ролик не записан; подготовлен план.

## Установка Android 328 — 11.10.2026

PASS: release-сборка build-android.ps1 -Action release.
PASS: APK без реализации симулятора установлен поверх текущего приложения по Wi-Fi
ADB 192.168.1.65:33719; подтверждены versionCode=328, versionName=1.0.328.
Первая потоковая установка прервана потерей ADB; после переподключения установка
--no-streaming завершена Success. Ориентация сохранена, восстановлена и проверена:
accelerometer_rotation=0, user_rotation=0. Данные приложения не удалялись.

## Инструмент записи деморолика — 11.10.2026

PASS: исходная и итоговая проверки build-android.ps1 -Action test; 71 debug + 71 release.
PASS: python tools/test-record-lock-demo.py — 9 проверок, включая запрет повторного
нажатия при потере связи, восстановление чтения после reconnect, исключение чужих
и отключённых элементов. Python-файлы компилируются.
PASS: короткий сценарий на физическом телефоне завершён; видео
out/demo-video/20261011-071426/00-smoke.mp4, 369178 байт, длительность MP4 23,9 секунды,
содержит видеотрек и media data. Зафиксированы переходы главное меню → настройки
приложения → главное меню. accelerometer_rotation/user_rotation после прохода 0/0.
Первый тест статической записи также прошёл. Промежуточные попытки остановлены:
обрывы ADB, неверное сопоставление описания/класса стрелки. Скорректировано управление
существующей стрелкой: её центральная точка перекрыта status bar, нажимается нижняя
часть границ. Системный Back не применяется. Интерфейс приложения не менялся.
Ограничения: WebView не предоставляет дочерние элементы UIAutomator на этом телефоне;
его эпизод записывается с оператором. Полный сценарий с настоящей дверью не выполнен.
Окончательный ролик не смонтирован. Скрипт не меняет системный режим уведомлений;
оператор готовит и восстанавливает его самостоятельно. APK не переустанавливался.
Изменённые файлы: AUDIT.md, CHANGELOG.md, TEST_REPORT.md, DEMO_VIDEO_PLAN.md,
record-lock-demo.ps1, tools/record-lock-demo.py, tools/test-record-lock-demo.py.

## Android 330 — меню настроек приложения, 11.10.2026

PASS: исходная debug-сборка проектным build-android.ps1 до изменений (329).
PASS: release-сборка проектным build-android.ps1 -Action release (330).
PASS: установка APK 1.0.330 по Wi-Fi ADB, версия подтверждена package dump.
Размер 545969 байт; SHA-256 9DD08F434F9E3A952CF498A8BD84800DAEE83AB7CA69447CFEEBE3ABCB48388B.
PASS: ориентация до установки сохранена, после установки восстановлена и повторно
прочитана 0/0; после запуска и UI-проверок остаётся 0/0.
PASS: два прохода главное меню → настройки → системный Back → главное меню.
По UIAutomator подтверждены новое название, отсутствие строки/стрелки и симулятора.
PASS: новое название открывает настройки TheKidLock в com.miui.securitycenter
на этом Xiaomi; системный Back возвращает сначала меню приложения, затем главный экран.
Первоначальное ожидание пакета com.android.settings в проверке уточнено под Xiaomi.
PASS: 8 проверок ADB-инструмента после адаптации к системному Back.
Android 26–32 на физическом устройстве не проверялись; сохранён legacy onBackPressed.
ESP не менялся, видео не записывалось.
Изменённые файлы: MainActivity.kt, strings.xml в values/values-en/values-ru/values-uk,
android_lock_beacon/BUILD_NUMBER, tools/record-lock-demo.py, tools/test-record-lock-demo.py,
AUDIT.md, CHANGELOG.md, TEST_REPORT.md, android_lock_beacon/CHANGELOG.md,
android_lock_beacon/TEST_REPORT.md.

## Пробный проход деморолика — 11.10.2026, приостановлен

Записаны автооткрытие/мощность BLE, включение и отдельное выключение удержания.
Все три MP4 в out/demo-video/20261011-072658 декодируются без ошибок.
UIAutomator пропустил краткое подтверждение удержания; запись подтверждает «Команда выполнена».
Выключение удержания выполнено отдельно, пользователь подтвердил красный индикатор.
Вернулись к автооткрытие=false и мощности=0. После остановки ориентация 0/0 восстановлена
и проверена. Настройки Web UI и интернет-открытие не записаны. Сценарии завершены по
указанию пользователя; продолжение описано в RESUME.md рядом с видео. Код не изменялся.

## Android 332 — дефолт отсчёта Web UI, 11.10.2026

PASS: исходная debug-сборка до изменений (331), итоговая release-сборка (332).
PASS: проверено изменение только двух значений 6 → 10; diff --check без ошибок.
PASS: Wi-Fi ADB установил release APK, package dump подтвердил 1.0.332/332.
Ориентация сохранена до установки, восстановлена и повторно проверена: 0/0.
Данные приложения не удалялись; отсчёт на чистых preferences отдельно на телефоне
не проверялся, чтобы не сбрасывать его настройки. Новые тесты для смены константы
не добавлялись. ESP не изменялся, запись видео остаётся приостановленной.
Изменённые файлы: LockSettings.kt, MainActivity.kt, android_lock_beacon/BUILD_NUMBER,
AUDIT.md, CHANGELOG.md, TEST_REPORT.md, android_lock_beacon/CHANGELOG.md,
android_lock_beacon/TEST_REPORT.md.


## Android 335 / ESP 486 — отсчёт и звуковой ползунок, 11.10.2026

PASS: исходные сборки до изменения кода: Android debug 333 и ESP signed-dev 485.
PASS: итоговая Android release 335, включая R8 и lintVital; первая попытка 334
обнаружила удалённую переменную hadPendingRequest, зависимость восстановлена,
повторная сборка успешна. ESP signed-dev 486 собран, RSA-подпись и обратная
расшифровка OTA проверены проектным скриптом. Размер приложения 0x141000,
свободно 33% OTA-раздела.
PASS: tools/test-departure-pulse.py — 29 сценариев реального C-кода, включая
0/120, отказ NVS, миграцию старого значения, выключение пробы, повторное включение.
PASS: tools/test-sound-slider.cjs — монотонность шкалы, все целые 0–120 мс,
обратное отображение, подписи трёх языков, API preview/save и откат при ошибке.
PASS: git diff --check. Android 1.0.335 установлен Wi-Fi ADB;
ориентация сохранена, восстановлена и подтверждена повторным чтением: 0/0.
OTA 486: попытка device-agent flash не началась, 192.168.1.54 не отвечает;
discover не обнаружил замок. Прошивка собрана, но установка не подтверждена.
Проверка физического звучания и нового Web UI на устройстве пока не выполнена:
телефон заблокирован. Видео остаётся приостановленным.
Изменённые файлы: MainActivity.kt, LockSettings.kt, webui/i18n/{ru,en,uk}.json,
main/app_config.{c,h}, main/main.c, main/web_ui.c, main/web/index.html,
tools/test-departure-pulse.py, tools/test-sound-slider.cjs, BUILD_NUMBER,
android_lock_beacon/BUILD_NUMBER, AUDIT.md, CHANGELOG.md, TEST_REPORT.md,
android_lock_beacon/CHANGELOG.md, android_lock_beacon/TEST_REPORT.md.

PASS: штатный tools/publish_esp32_release.py выполнил расшифровку OTA-контейнера во временный каталог, проверил RSA-подпись и соответствие payload версии 10.0.486. Релиз esp-v10.0.486 опубликован. SHA-256 локального и публичного latest-файла совпал: 288fc309567d19fa22ac2752a9b6699d4b9cd0e1d7108490f70b221fad39ab61. Publisher удалил предыдущий ESP-релиз и тег, оставив текущий; Android-релиз сохранён. Установка на замок не выполнялась, её выполняет пользователь через приложение.

## Android debug — чёрная шестерёнка — 11.10.2026

PASS: исходная debug-сборка 335 до изменения. PASS: итоговая debug-сборка 335, BUILD SUCCESSFUL. Изменён только tint левой иконки кнопки «Настройки замка»; общий векторный ресурс не менялся, шестерёнка в шапке осталась синей. APK не устанавливался на устройство. Изменённые файлы: android_lock_beacon/app/src/main/java/com/blelock/app/MainActivity.kt, AUDIT.md, CHANGELOG.md, TEST_REPORT.md, android_lock_beacon/CHANGELOG.md, android_lock_beacon/TEST_REPORT.md.

Дополнение: Release 1.0.338 после сборки установлен на телефон по Wi-Fi ADB. До установки сохранены accelerometer_rotation=0 и user_rotation=0; после установки восстановлены и повторно прочитаны те же значения.


## Android release 1.0.342 — прогресс подключения Web UI, 11.10.2026

PASS: исходная Android debug-сборка 338 до изменений. PASS: итоговая release-сборка
1.0.342 (versionCode 342), R8/lintVital успешны. Первая итоговая попытка выявила
отсутствующие fallback строки новых этапов; добавлены значения по умолчанию и повторная
сборка прошла. Ранний ACTION_WEB_UI_IP_REQUEST при нажатии убран; запрос IP этапа страницы
запускается после ACTION_WIFI_PROVISIONING_DISCONNECTED, когда замок уже прочитал hotspot
credentials. Удалён 10-секундный countdown. WebUiProgressButton рисует пять цветных стрелок;
текущий этап меняет подпись кнопки. Тексты ru/en/uk. Финальный Release 1.0.342 установлен на телефон по Wi-Fi
ADB. Ориентация до и после: accelerometer_rotation=0, user_rotation=0.
Изменённые файлы: MainActivity.kt, WebUiProgressButton.kt, values/{strings.xml},
values-{ru,en,uk}/strings.xml, AUDIT.md, CHANGELOG.md, TEST_REPORT.md,
android_lock_beacon/CHANGELOG.md, android_lock_beacon/TEST_REPORT.md.
Физический проход подключения к замку не проводился.

## Android release 1.0.345 — четыре стрелки Web UI — 11.10.2026

PASS: исходная debug-сборка 1.0.342. Итоговая release-сборка 1.0.345
(versionCode 345) успешна, включая R8 и lintVital. Кнопка переименована в
«К настройкам замка» (переводы ru/en/uk); четыре стрелки прогресса используют
единый цвет активного состояния, статусная строка и отдельный пятый этап убраны.
Фон основной иконки приложения изменён на жёлтый. APK установлен на телефон
по Wi-Fi ADB. До и после установки: accelerometer_rotation=0, user_rotation=0.
Физический проход подключения к замку не выполнялся.

## MQTT-дашборд замка — 11.10.2026

PASS: исходные Android Release и ESP32 сборки успешны до изменений. PASS: финальная
Android Release 1.0.348 (versionCode 348) прошла R8/lintVital; финальная ESP32 сборка
10.0.491 подписана, проверена, зашифрована, OTA загружена на текущий замок. Agent
подтвердил перезапуск и переход 10.0.490 → 10.0.491. APK установлен на телефон;
`accelerometer_rotation=1` и `user_rotation=0` сохранены и повторно совпали после
установки. Дверь классифицируется только по пластине (CONTACT/GAP 1–5/FAR). Живой retained MQTT state принят приложением: на экране показаны запертый
замок, закрытая дверь и 3,93 В. После следующего обновления state отображается заново;
краш приложения в свежем logcat не обнаружен. Физическое открывание двери для проверки
переходов «приоткрыта/открыта» не выполнялось.

## 11.10.2026 — отдельный MQTT-дашборд

До изменений прошли Android Release 1.0.349 и ESP32 baseline сборки через проектные скрипты. После изменений Android Release 1.0.350 (versionCode 350) прошла R8 и lintVital; ESP32 encrypted OTA сборка 10.0.493 прошла подпись и encrypt/decrypt round-trip. OTA на замок завершилась успешно: 10.0.491 → 10.0.493, agent подтвердил новую версию и перезапуск. APK установлен на физический телефон. До установки сохранены, после установки и запуска повторно проверены `accelerometer_rotation=1` и `user_rotation=0`.

На экране проверена отдельная панель дашборда непосредственно над карточкой автооткрытия. Приложение приняло подписанное MQTT-состояние: замок заперт, дверь приоткрыта, батарея 3,93 В. Это подтверждает BLE-признак настроенной домашней сети, подписку и обработку текущего состояния. Отрицательный сценарий с телефоном без owner-доступа и физическое отключение домашней сети отдельно не воспроизводились; их скрытие проверено по условиям отображения в Android-коде. Публикация ESP ограничена интервалом 500 мс. Нового `FATAL EXCEPTION` приложения в проверенном фрагменте logcat нет.

## 11.10.2026 — согласование состояния двери и замка

До изменений прошли Android Release 1.0.351 и ESP32 signed-dev baseline 10.0.494. После изменений Android Release 1.0.352 (versionCode 352) прошла R8/lintVital; ESP32 signed-dev 10.0.495 успешно подписана, проверена и прошла encrypt/decrypt OTA round-trip. Из десяти адресов MQTT перед сборкой девять ответили CONNACK; один временно не ответил, проектный broker-check исключил его и продолжил со списком из девяти работающих адресов.

ESP OTA завершилась успешно: 10.0.493 → 10.0.495, device-agent подтвердил перезапуск и версию. APK 1.0.352 установлен на телефон; до установки сохранены и после установки/запуска проверены `accelerometer_rotation=1`, `user_rotation=0`. UI hierarchy после получения нового MQTT сообщения показала «Замок заперт, Дверь закрыта, аккумулятор 3,93 V». Проверено также по коду: при `LOCKED` MQTT payload содержит `door=closed`, Android принудительно отображает `closed` при `lock=locked`, включая retained сообщения старой прошивки. Геометрию открытой дужки проверил по Canvas path: край вынесен за корпус с видимым зазором. Физический переход замка в открытое состояние отдельно не проверялся.

Изменены: `main/cloud_commands.c`, `LockDashboardView.kt`, `AUDIT.md`, `CHANGELOG.md`, `TEST_REPORT.md`, `android_lock_beacon/CHANGELOG.md`, `android_lock_beacon/TEST_REPORT.md`, `BUILD_NUMBER`, `android_lock_beacon/BUILD_NUMBER`, `android_lock_beacon/install-rotation-before-352.json`.

## Публикация ESP 10.0.495 — 11.10.2026

PASS: GitHub release `esp-v10.0.495` создан и опубликован как latest. Штатный publisher проверил RSA-подпись и соответствие payload сборке/версии, скачал публичный latest-файл и подтвердил идентичность SHA-256: `df48bd54166b067d6e1f25f01da38bc911a3c86377d0896e9176478d05377b1b`. Удалён один предыдущий ESP-релиз и один старый тег; Android-релиз сохранён. Публичная страница подтверждает `esp-v10.0.495` со статусом Latest.

## 11.10.2026 — закрытие после BLE-автооткрытия

PASS: ESP32 signed-dev сборка 10.0.497 через `build-esp32.ps1` / ESP-IDF 5.4.4.
PASS: RSA подпись образа проверена; encrypted OTA контейнер успешно зашифрован и
расшифрован для проверки magic/целостности; app partition свободен на 33%.
PASS: проектная проверка брокеров приняла 9 из 10 адресов; `broker.hivemq.com`
в этой сборке не ответил до таймаута и был исключён из build-time списка.
Изменения кода: `main/lock_control.c`; аудит: `AUDIT.md`; журнал: `CHANGELOG.md`.
Ограничение проверки: физический цикл двери и запрет BLE открытия на устройстве ещё
не воспроизводились. OTA на замок и проверка firmware_version выполняются после этого
отчёта штатным device-agent.

OTA result: PASS. Device-agent confirmed reboot and firmware version change on the current lock from 10.0.495 to 10.0.497 at 192.168.1.54. eFuse and irreversible protection settings were not changed. Physical door-transition and registered-beacon rejection timing were not manually exercised; those remain hardware scenario checks.

## 11.10.2026 — уточнение порога перехода двери

PASS: ESP32 signed-dev сборка 10.0.498 через `build-esp32.ps1` / ESP-IDF 5.4.4.
PASS: RSA подпись образа валидна; encrypted OTA контейнер успешно создан и прошёл
decrypt/magic проверку; размер приложения 0x141000 байт, запас раздела 33%.
PASS: build-time проверка приняла все 10 MQTT-брокеров.
Изменён `main/lock_control.c`: блокировка теперь требует `PLATE_FAR`, затем
`PLATE_CONTACT`; одних GAP-состояний недостаточно. Обновлён `AUDIT.md` и
`CHANGELOG.md`. Текущий замок был недоступен device-agent при проверке до OTA;
после сборки выполню штатную попытку обновления. Физический цикл на устройстве
пока не проверен.

OTA result: PASS. Device-agent подтвердил перезапуск текущего замка и переход версии
10.0.497 → 10.0.498 в ответе flash-команды после reboot. Отдельный последующий info-запрос уже не ответил: устройство снова стало недоступно по сети. Новая логика
требует именно FAR→CONTACT; физический сценарий закрытой двери с маяком после
исправления ожидает повторной проверки пользователем.

## 11.10.2026 — ползунок защитной задержки Web UI

PASS: исходная Android debug 1.0.353 сборка до локализации и финальная Android debug
1.0.354 сборка через проектный скрипт. PASS: ESP32 signed-dev 10.0.499 собрана;
RSA-подпись проверена, encrypted OTA round-trip успешен, запас app-раздела 33%.
Build-time проверка приняла 9 из 10 MQTT-брокеров (`broker.emqx.io` не ответил до
таймаута). OTA успешна: device-agent подтвердил перезапуск замка и смену 10.0.498 →
10.0.499. PASS: `tools/test-sound-slider.cjs` проверяет значение/сохранение защитной
задержки 3000–8000 мс, локализации и прежнее поведение звукового ползунка; ru/en/uk
JSON валидны. Проверка теста сначала выявила, что тестовый DOM не содержит новый
элемент; тестовый стенд дополнен и повторный запуск прошёл.

Изменены `main/app_config.h`, `main/app_config.c`, `main/lock_control.c`,
`main/web_ui.c`, `main/web/index.html`, `android_lock_beacon/app/src/main/assets/webui/i18n/{ru,en,uk}.json`,
`tools/test-sound-slider.cjs`, `AUDIT.md`, `CHANGELOG.md`, `TEST_REPORT.md`, а также
счётчики сборок ESP/Android. Подписанный release APK 1.0.355 собран для ручной установки. В настройках добавлен ползунок после задержки закрытия:
3–8 секунд, шаг 0,1 секунды, default 3 секунды; значение хранится в NVS. Android APK
собран, но не установлен: `adb devices` не обнаружил подключённых устройств.
Установка debug APK не выполнена: ADB Wi-Fi `192.168.1.65:33719` не отвечает, список
ADB-устройств пуст. Из-за отсутствия устройства системные параметры ориентации не
читались и не менялись.

## Публикация Android 1.0.355 и ESP 10.0.499 — 11.10.2026

PASS: GitHub Android release `app-v1.0.355` опубликован; release APK signer совпадает
с постоянным сертификатом, публичная загрузка APK и `thekidlock-update.json` совпали
с локальными SHA/metadata. SHA-256 APK:
`ff0f253a90e1190716e266c8c5db3ba4030a38f84ff287f25e924107d2724947`.
PASS: GitHub ESP release `esp-v10.0.499` опубликован как firmware latest. OTA-контейнер
подписан RSA, после расшифрования payload побайтно соответствует проверенной сборке.
Публичный latest `ble_lock_stage1_s3.bin` совпал по SHA-256:
`cae1a95904fc42cf2f4bb6055ec061552b18e820d58ca50d4c2`.
Удалён предыдущий Android release `app-v1.0.306`; ESP publisher удалил один предыдущий
релиз и старый тег. Глобальный latest firmware остаётся `esp-v10.0.499`.

## Android — интернет-кнопка только владельцу, build 357
Проверка до изменений: `build-android.ps1 -Action debug` успешно собрал baseline 356. Функциональная проверка: роль без owner-доступа устанавливает кнопку в `GONE`; роль владельца показывает кнопку. Команда и сетевой протокол не изменялись.

Результат: итоговая подписанная release-сборка 357 прошла успешно. Установка не выполнена: ADB Wi-Fi устройство 192.168.1.65:33719 недоступно, список `adb devices -l` пуст; системные настройки ориентации не читались и не менялись. APK: `android_lock_beacon/app/build/outputs/apk/release/app-release.apk`.

## Публикация Android 1.0.357 — 11.10.2026
PASS: publisher проверил подпись APK постоянным сертификатом, загрузил APK и update manifest, скачал публичную копию и подтвердил SHA-256. Опубликован `app-v1.0.357`; Android 1.0.355 удалён, ESP global latest сохранён. Установка на телефон не выполнялась: ADB-устройство недоступно.

## Итог Web UI — порог зоны открытия в dBm, ESP 10.0.502 — 11.10.2026
- PASS: до изменений проектный baseline `finalize-existing` собран как 10.0.500.
- PASS: `node tools/test-sound-slider.cjs` проверил отображение −68/−90 dBm, обновление при перемещении, восстановление значения из `/api/config`, существующие регулировки задержки и звука.
- PASS: итоговая ESP 10.0.502 собрана проектным скриптом для профиля `finalize-existing`; RSA-подпись и encrypted OTA round-trip подтверждены. Проверка доступности брокеров: 8 ответили успешно, часть публичных адресов временно не ответила.
- Промежуточный образ 10.0.501 собран в профиле `signed-dev`. Позднее проверка опубликованного релиза и журнала OTA подтвердила, что работающая на замке 10.0.499 использует именно этот профиль. Первоначальный вывод о несовместимости 10.0.501 был ошибочным; для обновления текущей 10.0.499 выбран 10.0.501.
- OTA 10.0.502 отправлен через `device-agent.ps1`; HTTP-передача завершилась, однако устройство не вернулось в Agent API за 120 секунд. До OTA замок сообщил 10.0.499, `LOCKED`, `fault_flags=0`, `calibration_valid=true`, `rom_download_disabled=true`. После OTA повторные info/discover не обнаружили замок, поэтому установленную версию подтвердить нельзя.
- Визуальная проверка экрана не выполнялась. Артефакт: `build-finalize-existing/encrypted/ble_lock_stage1_s3.bin`.

Итоговая проверка: `node tools/test-sound-slider.cjs` успешно проверил новое отображение dBm и сохранённые настройки. ESP 10.0.502 собрана в профиле `finalize-existing`; подпись и расшифровка OTA контейнера прошли, но профиль отличается от установленной 10.0.499. OTA-запрос принят, но устройство не вернулось в Agent API за 120 секунд, поэтому версия после OTA не подтверждена.

## Повторная попытка OTA 10.0.502 — 11.10.2026
`device-agent.ps1 -Action flash` остановился до передачи образа: сохранённый замок `192.168.1.54` не отвечает на Agent API. `discover` по ARP и проверка всей домашней подсети 192.168.1.0/24 по прежнему `device_id=a0:f2:62:f1:14:b8` не обнаружили замок. ADB телефона `192.168.1.65:33719` отказал в подключении. Повторная передача не началась; текущая версия и состояние замка остаются неподтверждёнными. Для продолжения требуется вернуть физический замок в сеть (например, перезапустить питание и проверить домашний Wi-Fi).

## Повторная OTA и публикация 10.0.501 — 11.10.2026
- После возврата замка в сеть Agent API подтвердил 10.0.499, `LOCKED`, `fault_flags=0`, валидную калибровку и `rom_download_disabled=true`.
- Вторая передача 10.0.502 завершилась HTTP-ответом, но замок не вернулся в Agent API за 180 секунд. Установка 10.0.502 не подтверждена; опубликованный релиз 499 и журнал прошлой установки показали, что профиль 502 отличается от рабочего профиля.
- Публикация `esp-v10.0.501` успешна: профиль `signed-dev`, RSA-подпись, decrypt round-trip и побайтовое совпадение с подписанной сборкой проверены publisher. Публичный latest asset SHA-256 `8f8e5cc21ad6307c1b87809b174abe397c21ecc13b5192ef356cab9f896e699f`; предыдущий ESP release/tag 499 удалены.
- Физическая установка 10.0.501 ещё не подтверждена: после второй попытки 10.0.502 замок снова не отвечает по сети.

PASS: GitHub release `esp-v10.0.501` после публикации повторно прочитан через API: он является latest, содержит один OTA asset и описание с обеими прямыми ссылками. На момент повторной проверки Agent API замка `192.168.1.54` всё ещё недоступен; локальная установка 10.0.501 не выполнялась.
## Android — удаление дашборда — 11.10.2026

- PASS: исходный `build-android.ps1 -Action debug`, build 358.
- PASS: итоговый `build-android.ps1 -Action release`, подписанный APK 1.0.359.
- PASS: `build-android.ps1 -Action test`, debug и release unit-тесты, включая
  оставшиеся сценарии `ServiceBeaconProtocolTest` (6/6 для каждой конфигурации).
- PASS: поиск по исходникам и ресурсам не нашёл ссылок на дашборд и его
  специальный запрос домашнего Wi-Fi.
- Первоначальный запуск тестов выявил устаревшее ожидание текста существующей
  кнопки «К настройкам замка»; ожидание приведено к фактическим переводам,
  повторный запуск успешен.
- Установка на телефон не выполнена: `192.168.1.65:33719` отказал в ADB-соединении,
  список устройств пуст. Настройки ориентации не читались и не менялись.
## Публикация Android 1.0.359 — 11.10.2026

PASS: publisher проверил постоянную подпись APK, пакет и versionCode 359,
загрузил APK и манифест, скачал публичные копии и сверил SHA-256
`6c457eeea7b97018af5847704fba7b1099d9f763fe4249e2f69372098e889cd4`.
Независимый запрос GitHub API подтвердил опубликованный `app-v1.0.359` с двумя
нужными файлами и глобальный latest `esp-v10.0.501`. Прежний app 357 удалён.
## Диагностика автоподключения Wi-Fi — 11.10.2026

- PASS: исходная ESP signed-dev сборка 10.0.503 проектным скриптом; подпись и
  шифрование OTA-контейнера проверены.
- PASS: Agent API до OTA сообщил 10.0.501, закрытый замок и `fault_flags=0`.
- PASS: штатная OTA и повторное чтение Agent API подтвердили 10.0.503.
- По коду подтверждено условие `auto_connect || mqtt_required`; Web UI
  сохраняет MQTT как включённый. Значения двух NVS-параметров на текущем
  устройстве напрямую не считаны: соответствующие Web API недоступны по
  Agent API и отвечают 404 вне телефонной сессии.
## Переключатель домашнего Wi-Fi — 11.10.2026

- PASS: исходная signed-dev сборка 10.0.504 до правок. OTA этого промежуточного
  образа не началась: Agent API замка временно не отвечал.
- PASS: итоговая signed-dev сборка 10.0.505; RSA-подпись и расшифровка
  зашифрованного OTA-контейнера проверены проектным скриптом.
- PASS: `device-agent.ps1 -Action flash` передал 10.0.505; замок вернулся
  после перезапуска, Agent API сообщил переход 10.0.503 → 10.0.505.
- По коду проверены запрет старта домашнего Wi-Fi, прекращение действующей
  сессии, блокировка повторной попытки MQTT и отсутствие запрета временной
  телефонной сессии. Физическое переключение Web UI не проверялось: доступ
  к этому API разрешён только через телефонную сессию.
## Android — значок и размер виджета — 11.10.2026

- PASS: исходный `build-android.ps1 -Action debug`, build 360.
- PASS: итоговый подписанный `build-android.ps1 -Action release`, build 361,
  включая `lintVital` и R8.
- PASS: SHA-256 исходного `image_1.png` и добавленного ресурса совпадает:
  `b532c38b20bc7bc76ae57b2a1eed65d53d7bb42f7fc0a385f636ca9c2d2049d2`.
- PASS: APK metadata содержит package `com.blelock.app`, versionCode 361 и
  графический значок приложения. Ресурсы виджета оставлены отдельными.
- Установка на телефон не выполнена: ADB `192.168.1.65:33719` отклонил
  подключение, `adb devices -l` пуст. Ориентация телефона не менялась.
- Изменение размера и вид значка на рабочем столе физического телефона
  визуально не проверены.
## Публикация Android 1.0.361 — 11.10.2026

PASS: publisher проверил пакет, номер 361 и постоянную подпись APK, загрузил
APK и манифест, скачал публичные копии и сверил SHA-256
`78e95ed7d8674a78588f5a8970315cac2bf3b48acad9ce7f1d8199c169c8f194`.
GitHub API подтвердил опубликованный релиз с обоими файлами и сохранение
`esp-v10.0.501` как global latest; прежний app-релиз 359 удалён.
## Android — виджет без метки и жёлтого фона — 11.10.2026

- PASS: исходная debug-сборка 362 до правок.
- PASS: итоговая подписанная release-сборка 363, включая R8/lintVital.
- PASS: в ресурсах не осталось ссылок на `ic_widget_shortcut_badge`;
  цвет фона закрытого и открытого значков совпадает (`#17324D`).
- Установка и визуальная проверка не выполнены: ADB
  `192.168.1.65:33719` отклонил подключение, список устройств пуст.
  Системные настройки ориентации не менялись.
## Публикация Android 1.0.363 — 11.10.2026

PASS: publisher проверил постоянную подпись, пакет и versionCode 363,
загрузил и публично перечитал APK/JSON. SHA-256 APK:
`f9d3a020477d9d15a809ec329e429cfc6b11e8ca27fb2a3a5504b95b646523a6`.
GitHub API подтвердил опубликованный релиз с двумя файлами и сохранение
`esp-v10.0.501` как global latest; прежний app-релиз 361 удалён.

## Локализация Android — 11.10.2026
- PASS: `build-android.ps1 -Action test`; 69 тестов, включая схему ресурсов и плейсхолдеры шести новых локалей.
- PASS: XML ресурсов разбирается, Web UI JSON сохраняет полный набор ключей.
- Release APK будет собран и проверен отдельно.
- Android 1.0.365: release собран и подписан, SHA-256 `4142037088333D68D9700BCDCE5EB236D3CD2B4F722FD16F8199B31653A933B8`. Установка на телефон не состоялась: ADB-устройство недоступно, Wi-Fi ADB отказал в подключении.
- PASS: GitHub app-релиз `app-v1.0.366`, публичный APK и metadata сверены по SHA-256 `15c89bdb9fbec9444f0df842e6766ceeba28702a915b1fb50700934e9e78aef6`; прежний app-релиз удалён, ESP latest сохранён.
- APK 1.0.366 не установлен: телефон недоступен по ADB (`192.168.1.65:33719`, connection refused); ориентация устройства не менялась.

## Мастер калибровки — 11.10.2026
- PASS: до и после изменений выполнен `build-android.ps1 -Action test`; полный перевод новых локалей, структура и форматные параметры проверены.
- Итоговый release build и публикация будут записаны после завершения.
- Android 1.0.367: GitHub-релиз опубликован и проверен по SHA-256 `90d3274e0dccc7b2bc235c5f6e4004ec9361852faa1d847e5fae104e73cb266a`; предыдущий Android-релиз удалён, ESP latest сохранён. APK не установлен: Wi-Fi ADB недоступен.
# 12.10.2026 — удалённый отзыв ключей: этап прошивки

- Исходная сборка signed-dev 512 прошла до изменений.
- Сборка signed-dev 514 после изменений успешна; криптографические self-test сохранены.
- Повторная сборка signed-dev 515 после защиты совпадающих адресов успешна; подпись и локальная проверка шифрованного OTA-образа прошли.
- Попытка OTA на `192.168.1.54` не удалась: устройство не ответило как Agent API v1. Проверка установки остаётся открытой.
- Прошивка 10.0.515 опубликована; скрипт проверил подписанный образ, шифрованный контейнер и SHA-256 публичной загрузки. Устройство по `192.168.1.54` повторно не ответило; `device-agent.ps1 discover` не нашёл ESP в ARP-кэше.
- Изменены: `main/cloud_admin.c`, `main/cloud_admin.h`, `main/cloud_commands.c`, `main/CMakeLists.txt`, `AUDIT.md`, `CHANGELOG.md`, `TEST_REPORT.md`.
# 12.10.2026 — диагностика тайм-аута удалённого отзыва

- До изменений успешно собраны ESP signed-dev 516 и Android debug/release tests 1.0.372; версия 516 загружена по OTA и подтверждена Agent API.
- После изменения журнала ESP signed-dev 517 собрана успешно; подпись и проверка шифрованного образа прошли.
- ESP signed-dev 518 с ранней отметкой MQTT-пакета собрана успешно; подпись и локальная проверка контейнера прошли.
- ESP signed-dev 519 с записью адреса брокера собрана успешно; подпись и локальная проверка контейнера прошли.
- Изменены `main/cloud_commands.c`, `main/cloud_admin.c`, `AUDIT.md`, `CHANGELOG.md`, `TEST_REPORT.md`.

- PASS: Android `build-android.ps1 -Action test` после перехода диагностики на `Log.println(ASSERT, ...)`.
- На нажатии UI показывает загрузку, но текущая проверка не зафиксировала MQTT-пакет от телефона на брокере/ESP; диагностика продолжается. APK не опубликован.

- PASS: `build-esp32.ps1` signed-dev; образ подписан RSA, проверен encrypted OTA контейнер.
- PASS: OTA на текущий ESP; Agent API подтвердил переход 10.0.519 → 10.0.521.
- Pending: считать reset_reason после следующего контролируемого запроса отзыва.
- Файлы: `main/main.c`, `AUDIT.md`, `CHANGELOG.md`, `TEST_REPORT.md`.

- ESP 10.0.521 после успешного OTA: запрос при MQTT subscription-ready приводит к временному исчезновению ESP и новому boot_id; reset_reason=4 (panic). Требуется следующий build с RTC-маркером стадии, затем OTA и повтор одного запроса.

- PASS: ESP signed-dev 10.0.522; подпись RSA и зашифрованный OTA-контейнер проверены.
- PASS: повторный OTA успешен, Agent API подтвердил 10.0.521 → 10.0.522; стартовый reset_reason=3 (штатный OTA reboot), MQTT subscription-ready подтверждён.
- Следующий шаг: один запрос на разблокированном телефоне; проверить RTC stage после завершения или panic.
- Изменены: `main/main.c`, `main/cloud_commands.c`, `main/cloud_admin.c`, `main/cloud_admin.h`, `AUDIT.md`, `CHANGELOG.md`, `TEST_REPORT.md`.

- ESP 10.0.522 OTA подтверждён; контрольный запрос получил PUBACK, после него ESP перезапустился с `reset_reason=4`.
- Предыдущий RTC-stage равен 0; причина пока не локализована. Следующий этап — stage marker в начале каждого `MQTT_EVENT_DATA`.

- PASS: ESP signed-dev 10.0.523; RSA signature и encrypted OTA-container verification успешны.
- PASS: OTA подтверждена Agent API: 10.0.522 → 10.0.523.
- Pending: проверить RTC stage после одного нажатия на разблокированном телефоне и MQTT connection-ready.
- Изменены: `main/cloud_commands.c`, `AUDIT.md`, `CHANGELOG.md`, `TEST_REPORT.md`.

- ESP 10.0.523: panic повторился; stage marker в RTC_DATA после перезагрузки отсутствовал. Диагностика обновлена на RTC_NOINIT + проверочную сигнатуру для сохранения через reset.

- PASS: ESP signed-dev 10.0.524; подпись и encrypted OTA контейнер проверены.
- PASS: OTA, Agent API подтвердил 10.0.523 → 10.0.524.
- Pending: проверить предыдущую RTC stage после контролируемого запроса.
- Изменены: `main/cloud_admin.c`, `AUDIT.md`, `CHANGELOG.md`, `TEST_REPORT.md`.

- PASS: invalid test payload `{}` получен MQTT admin handler, отклонён дешифрованием без panic; RTC stage 7 сохранён через штатный restart и выведен при старте.
- BLOCKED FOR DEVICE REPRO: Wi-Fi ADB телефона отключился/стал unavailable; версия 10.0.524 на ESP активна. Нужен повтор реального запроса из приложения после восстановления ADB.

### Исправление тайм-аута отзыва ключей — 12.10.2026

- PASS baseline: `build-esp32.ps1` собрал signed-dev ESP; перед диагностической правкой образ 10.0.525 загружен OTA, Agent API подтвердил переход с 10.0.524 на 10.0.525.
- PASS repro: телефон подключён через ADB `192.168.1.65:34333`; кнопка вызвала публикацию admin list-запроса и PUBACK. ESP перезагрузился (`reset_reason=4`), RTC marker 16 локализовал сбой в синхронной MQTT-публикации ответа.
- Изменение: admin reply отправляется через неблокирующий outbox `esp_mqtt_client_enqueue()`.
- PASS: ESP signed-dev 10.0.526 собран; проверены RSA-подпись и encrypted OTA round-trip; OTA завершена, Agent API подтвердил 10.0.525 → 10.0.526.
- Pending: повторить полный запрос из приложения и подтвердить получение списка телефонов без перезапуска ESP. На момент проверки экран телефона заблокировался.
- Изменены: `main/cloud_admin.c`, `AUDIT.md`, `CHANGELOG.md`, `TEST_REPORT.md`.

### Проверка стека после MQTT-ответа — 12.10.2026

- PASS repro на 10.0.526: приложение опубликовало owner list-запрос; ESP перезапустился с `reset_reason=4`, а стартовый RTC marker равен 17. Вызов `esp_mqtt_client_enqueue()` вернулся; сбой находится после него.
- Изменение: `cloud_commands` стек увеличен с 8192 до 12288; после admin handler временно пишется RTC stage 26 и остаток стека.
- PASS: ESP signed-dev 10.0.527 собран и загружен OTA; Agent API подтвердил переход 10.0.526 → 10.0.527.
- PASS device test: на ADB-порту 34333 приложение получило два list-ответа и status=ok на apply. ESP сохранил boot_id; event log показывает `command stack free=3828` на обоих проходах. Это соответствует примерно 8460 байтам занятого стека и объясняет panic при старом размере 8192.
- Очистка: временные trace markers, reset-reason и stack-watermark логи удалены; оставлены эксплуатационные события запроса/ответа.
- PASS final ESP signed-dev 10.0.528: build, RSA signature verification и encrypted OTA round-trip прошли; OTA подтвердила переход 10.0.527 → 10.0.528.
- PASS final device test: Android 1.0.374 через ADB 34333 отправил list-запрос; получил расшифрованный ответ `status=ok`. Agent API подтвердил 10.0.528 и прежний boot_id после запроса; события ESP показывают успешную публикацию ответа без panic.
- Изменены: `main/cloud_commands.c`, `main/cloud_admin.c`, `main/cloud_admin.h`, `main/main.c`, `AUDIT.md`, `CHANGELOG.md`, `TEST_REPORT.md`.

### Исключение владельца из списка отзыва — 12.10.2026

- PASS baseline: ESP 10.0.529 build+OTA подтвердили исходное состояние; Android debug build успешен.
- Изменения: прошивка фильтрует владельца из snapshot, apply-mask и ответа; Android принимает ответ без owner-поля и пустой список. Шрифт имён и адресов в чек-листе уменьшается `RelativeSizeSpan(0.5f)`. Empty state переведён для всех локалей приложения.
- PASS: Android debug/release сборки и `build-android.ps1 -Action test`; unit tests завершились успешно.
- PASS: ESP signed-dev 10.0.530 собран, RSA подпись и encrypted OTA round-trip проверены; OTA подтвердил 10.0.529 → 10.0.530.
- PASS: release APK 1.0.377 установлен поверх существующего приложения; значения `accelerometer_rotation`/`user_rotation` были 0/0 и восстановлены/проверены как 0/0. Первая попытка debug APK отклонена из-за несовпадения подписи, настройки ориентации также восстановлены.
- Pending device UI check: после установки телефон заблокировался; для проверки пустого сообщения нужен разблокированный экран. Из приложения отзыв не применялся после установки 1.0.377.
- Файлы: `main/cloud_admin.c`, `android_lock_beacon/app/src/main/java/com/blelock/app/RemoteKeys.kt`, `android_lock_beacon/app/src/main/java/com/blelock/app/MainActivity.kt`, `android_lock_beacon/app/src/main/res/values*/strings.xml`, `AUDIT.md`, `CHANGELOG.md`, `TEST_REPORT.md`.

## Android: иконка отзыва ключей и геометрия кнопок — 12.10.2026

- PASS baseline: `build-android.ps1` debug-сборка успешна до изменений.
- PASS: `build-android.ps1 -Action test`; debug/release compile и unit tests успешны.
- PASS: `build-android.ps1 -Action release`; release APK 1.0.387 собран с lintVital.
- PASS: `git diff --check` (только предупреждения Git о нормализации CRLF в рабочей копии).
- NOT INSTALLED: `adb devices` не обнаружил устройств; системные параметры ориентации не менялись.
- Изменены для задачи: `MainActivity.kt`, `ic_handoff_revoke_key.xml`; обновлена документация проекта.
- УСТАНОВЛЕНО после подключения ADB `192.168.1.65:38489`: приложение `1.0.387` подтверждено через Package Manager. `accelerometer_rotation` и `user_rotation` до и после установки — `0/0`; сохранён снимок `android_lock_beacon/install-rotation-before-387.json`.

## Android: настройки приложения и отзыва ключей — 12.10.2026

- PASS baseline: Android debug-сборка успешна перед изменениями.
- PASS: `build-android.ps1 -Action test` — debug/release compile и unit tests успешны.
- PASS: `build-android.ps1 -Action release` — release APK 1.0.389 собран.
- PASS: APK 1.0.389 установлен на `192.168.1.65:38489`; Package Manager подтвердил версию. `accelerometer_rotation` и `user_rotation` были `0/0` и остались `0/0`.
- Файл снимка параметров перед установкой: `android_lock_beacon/install-rotation-before-389.json`.
- Для Android-этапа изменены `MainActivity.kt` и `values*/strings.xml` для `revoke_keys`.

## Android settings и Web UI feedback — 12.10.2026

- PASS Android baseline: debug-сборка без изменений прошла.
- PASS Android: `build-android.ps1 -Action test` — debug/release compile и unit tests успешны; release APK 1.0.389 собран и установлен через ADB. Параметры ориентации `0/0` сохранены и восстановлены.
- PASS ESP baseline: `build-esp32.ps1` до изменений, signed-dev build 10.0.551.
- PASS ESP: signed-dev 10.0.552 собрана; проверены RSA-подпись и зашифрованный OTA round-trip.
- PASS Web UI tests: `node tools/test-sound-slider.cjs`; inline script прошёл `node --check`.
- PASS OTA: Agent API подтвердил переход замка 10.0.550 → 10.0.552 без смены устройства.
- Файлы задачи: `android_lock_beacon/app/src/main/java/com/blelock/app/MainActivity.kt`, Android локали `values*/strings.xml`, `main/web/index.html`, `tools/test-sound-slider.cjs`, документы `AUDIT.md`, `CHANGELOG.md`, `TEST_REPORT.md` и Android отчёты.

# 12.10.2026 — регулятор приложения и обратная связь крутилок

- PASS Android debug/release tests, release APK 1.0.389 и установка по ADB; ориентация `0/0` восстановлена.
- PASS ESP signed-dev baseline 10.0.551 и итоговая 10.0.552; подпись и шифрованный OTA образ проверены.
- PASS: `node tools/test-sound-slider.cjs` и `node --check` встроенного JavaScript.
- PASS OTA: замок обновлён 10.0.550 → 10.0.552, версия проверена через Agent API.

## Android и Web UI — 12.10.2026

- PASS: baseline Android build до изменений.
- PASS: `build-android.ps1 -Action test` — debug/release compilation и unit tests.
- Проверка установки APK и ESP сборка/OTA выполняются после сборки release и ESP-части.

- PASS release Android APK 1.0.391 собран и установлен на `192.168.1.65:38489`; ориентация `accelerometer_rotation/user_rotation` до и после установки совпала: `0/0`.
- PASS Web UI: `node tools/test-sound-slider.cjs`; проверены фон под значением крутилки и отсутствие анимации самого числа.
- PASS release APK 1.0.391 установлен через ADB; значения ориентации до/после `0/0`.
- PASS ESP 10.0.554: signed-dev build, RSA signature verification, encrypted OTA round-trip; OTA замка 10.0.552 → 10.0.554 подтверждена Device Agent.
- PASS: `node tools/test-sound-slider.cjs` — SVG-подложка числа, диапазоны крутилок и их сохранение.

- PASS после добавления проверок HMAC таймера и округления секунд: uild-android.ps1 -Action test завершился успешно (debug/release).
- PASS после OTA: Agent API повторно подтвердил irmware_version=10.0.554.

## Единая кнопка автооткрытия и круг крутилки — 12.10.2026

- PASS baseline Android debug и ESP signed-dev 10.0.555; baseline ESP по OTA подтверждена Device Agent.
- PASS `build-android.ps1 -Action test`: Android debug/release compile и unit tests.
- PASS `node tools/test-sound-slider.cjs` и `node --check` встроенного JavaScript.
- PASS `build-esp32.ps1`: signed-dev 10.0.556, RSA-подпись и encrypted OTA round-trip.
- PASS OTA: Device Agent подтвердил обновление замка 10.0.555 → 10.0.556.

- PASS: release APK 1.0.393 установлен по ADB; настройки ориентации до и после `0/0`.
- PASS UI Automator: все пять видимых кнопок главного экрана и кнопка системных настроек приложения имеют высоту 277 px на текущем телефоне.
- PASS: повторный Agent API `firmware_version=10.0.556` и тот же `device_id`.
- Изменённые файлы этапа: `android_lock_beacon/app/src/main/java/com/blelock/app/MainActivity.kt`, `android_lock_beacon/app/src/main/res/drawable/bg_handoff_card_active.xml`, `ic_handoff_check.xml`, десять `values*/strings.xml`, `main/web/index.html`, `tools/test-sound-slider.cjs`, `AUDIT.md`, `CHANGELOG.md`, `TEST_REPORT.md` и Android отчёты.

## Виброотклик и стрелки крутилок — 12.10.2026

- PASS: до изменений Android debug и ESP signed-dev 10.0.557 собраны проектными скриптами; замок не ответил Device Agent, исходная сборка по OTA не установлена.
- PASS: после изменений `build-android.ps1 -Action test`, `build-android.ps1 -Action release`, `node tools/test-sound-slider.cjs`, проверка синтаксиса встроенного JavaScript и `build-esp32.ps1` выполнены успешно. ESP signed-dev 10.0.558 прошла RSA-проверку и encrypted OTA round-trip.
- PASS: release APK 1.0.395 установлен на телефон `192.168.1.65:38489`; `accelerometer_rotation/user_rotation` до и после `0/0`. UI Automator подтвердил одинаковые границы по горизонтали `[144,936]` для девяти языков, системной кнопки и ссылки проекта.
- NOT INSTALLED: Device Agent по `192.168.1.54` не ответил; поиск не нашёл ESP в ARP-кэше. OTA 10.0.558 и версия на замке не подтверждены.
- Изменённые файлы: `main/web/index.html`, `android_lock_beacon/app/src/main/java/com/blelock/app/{ButtonHaptics.kt,MainActivity.kt,SettingsUi.kt}`, `BUILD_NUMBER`, `android_lock_beacon/BUILD_NUMBER`, `AUDIT.md`, `CHANGELOG.md`, `TEST_REPORT.md`, `android_lock_beacon/CHANGELOG.md`, `android_lock_beacon/TEST_REPORT.md`.

## Подписи Web UI крутилок — 12.10.2026

- PASS baseline ESP signed-dev 10.0.559; OTA замку подтверждена Device Agent.
- PASS: `build-esp32.ps1` собрал ESP signed-dev 10.0.560; подпись RSA и encrypted OTA round-trip проверены сборочным скриптом.
- PASS OTA: Device Agent подтвердил обновление замка 10.0.559 → 10.0.560.
- Изменены `main/web/index.html`, `BUILD_NUMBER`, `AUDIT.md`, `CHANGELOG.md`, `TEST_REPORT.md`.
