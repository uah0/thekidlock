# Проверки текущего выпуска — 10.10.2026

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
Android 263 установлен, ориентация восстановлена 0/0. Скриншот собственного приложения подтверждает читаемую динамическую подпись; интернет-команду в этот момент запускал пользователь, агент команду замку не отправлял. Установка ESP446 агентом не выполнялась. Артефакты — production/esp-10.0.446 и verification/internet-labels-446.
PASS: итоговая Android сборка 265 (45с), 110 unit tests без ошибок (59с). APK265 установлен на физический телефон; ориентация сохранена/восстановлена/проверена 0/0. В видимом тексте осталось ровно пять заданных подписей; ожидание closing/ошибка сохраняют предыдущий этап, что проверяется отдельным тестом reducer. Самостоятельная установка ESP446 остаётся за пользователем.

## Диагностика отсутствия «Дверь открылась» — 10.10.2026
В private firmware_transfer телефона downloaded_version=10.0.446, последняя отмеченная передача текущему замку sent=10.0.444. Прошивка 444 публикует шесть полей без door; новый Android не подменяет closing подтверждением открытия двери. Данные для этого этапа добавлены в уже опубликованной ESP446, требуется пользовательская установка этой версии. Фактическая версия ESP не подтверждена: сохранённый адрес 192.168.1.54 недоступен. Изменений функционального кода не выполнено. Диагностические baseline Android266 и ESP447 успешно собраны, не устанавливались и не публиковались; рабочий APK265 и публичная прошивка446 сохранены. Изменены автоматически только BUILD_NUMBER/Android BUILD_NUMBER и отчёты.

## Замена названия ESP32 на замок — 10.10.2026
PASS: baseline Android267 и ESP448, итоговые Android268 (42с) и ESP449 штатными проектными скриптами. PASS: XML валиден; пользовательские строковые литералы Android/resources/Web UI не содержат ESP32; JavaScript обоих Web UI файлов проверен node --check. PASS: подписанный encrypted OTA449 расшифрован и сравнен с signed plaintext, RSA подпись verified. PASS: public latest449, SHA256 6ab5bccf888e7a544754a00063eb90645235a8723e82ef51b538034c87b312f4, размер1380864. Publisher оставил единственный актуальный релиз.
BLOCKED: APK установка отменена телефоном, INSTALL_FAILED_USER_RESTRICTED: Install canceled by user. accelerometer_rotation/user_rotation сохранены перед попыткой, восстановлены и повторно прочитаны 0/0. APK268 сохранён; установку и изменение интерфейса на физическом устройстве не подтверждаем. ESP449 не прошивалась агентом, пользователь устанавливает сам. Unit tests повторно не запускались: изменения только текста, управление/протоколы/алгоритмы не менялись.

Повторная установка по указанию пользователя: APK268 успешно установлен через ADB --no-streaming (Success), MainActivity запущена. accelerometer_rotation/user_rotation перед установкой сохранены, после установки/запуска восстановлены и проверены: 0/0. Предыдущее ограничение установки снято.
