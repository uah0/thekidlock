# Состояние после очистки — 10.10.2026

## 12.10.2026 — оформление главных кнопок

- PASS: Android debug/release unit tests и signed release APK 1.0.385.
- PASS: release `app-v1.0.385` опубликован; APK и metadata скачаны без авторизации,
  SHA-256 APK `f804812a7e4ee2fda632c88aa95f61ccd4384f8135ba3b08a798cd0b667b2a6c`.
- PASS: предыдущий Android-релиз 1.0.383 удалён.
- NOT INSTALLED: ADB не обнаружил телефон, установка APK не выполнялась.
- Изменены `MainActivity.kt`, `InternetOpenProgressButton.kt`,
  `bg_handoff_card.xml`, добавлен `ic_handoff_revoke_key.xml`.

## 12.10.2026 — тактильный отклик крутилок Web UI

- PASS: baseline Android debug 1.0.382; итоговые Android unit tests и signed release APK 1.0.383 успешны.
- NOT INSTALLED: ADB сейчас не показывает устройств; параметры ориентации не менялись.
- Код вызова ограничен доверенным Web UI и использует Android `CLOCK_TICK`, соблюдая глобальную настройку touch feedback.

## 12.10.2026 — шрифт списка отзыва ключей

- PASS: debug baseline и release APK 1.0.381.
- PASS: APK установлен; параметры ориентации экрана сохранены и восстановлены.
- PASS: текст диалога использует системный размер вместо RelativeSizeSpan 0.5×.

## Короткие MQTT-топики — 12.10.2026

- PASS: Android debug/release unit tests, release APK 1.0.371 и проверка подписи при публикации. Публичные APK/JSON проверены, SHA-256 APK `ac51e9b43a894c4ae19a99657005b4fe3d21b1e3799d7297ae9e4b75b3fc048a`.
- PASS: совместимая прошивка 10.0.511 установлена на замок и опубликована.
- NOT INSTALLED: ADB `192.168.1.65:33719` отклонил подключение. Установка APK и реальное интернет-открытие не подтверждены; настройки ориентации не менялись.

## Шифрование интернет-открытия — 12.10.2026

- PASS: проектные Android debug/release unit tests; тест HKDF совпал с независимо вычисленным вектором. Debug APK 1.0.369 собран, SHA-256 `8a7a58ff3d2ac0ac4b5ac8498e4aa154bd4b75cbf3f1c5fd8ff40d51d94e13df`.
- NOT INSTALLED: Wi-Fi ADB `192.168.1.65:33719` отказал в подключении, устройств нет. Ориентация телефона не менялась.
- Во время первой проверки совместное MQTT-открытие не выполнялось: замок по сохранённому адресу `192.168.1.54` временно не отвечал.
- После восстановления связи замок обновлён через OTA до 10.0.509, самопроверка криптографии и MQTT-подключение подтверждены.
- PASS: подписанный release APK 1.0.370 опубликован как `app-v1.0.370`, публичный APK и update metadata проверены; SHA-256 `14ddda058750734f8234c9d3e8c7239ba3e1cc056ae11929ebcdda47231f916e`. Телефон всё ещё недоступен по ADB; фактическая установка и интернет-открытие не подтверждены.

Сохранён локальный debug APK: ../production/android-debug/app-debug.apk; SHA-256 4cddcb3b161278b8713c30aae93f60f342f8a44e39579d772430577ae71f0c91. Исходники, Gradle wrapper и настройки проекта сохранены. Новая Android сборка и установка не выполнялись. Текущее установленное приложение использовалось для подтверждённой установки OTA 10.0.436 и повторного открытия Web UI. Перед будущей установкой APK сохранить и восстановить accelerometer_rotation/user_rotation согласно AGENTS.md.

## Индикатор обновления — 255
PASS: baseline/final debug проектным скриптом; 106 unit tests, zero failures/errors; assembleDebugAndroidTest. PASS: реальное скачивание 10.0.440 GitHub в private storage, hash совпал; главная кнопка содержит меньшую строку обновления; после пользовательского начала OTA stored sent=downloaded=440. PASS: native bridge/private file loopback instrumentation, без прошивки ESP. Temporary fixture metadata восстановлены; runner cleanup перед finish исправлен и собран. APK255 установлен, rotation 0/0 сохранена/восстановлена. Реальную версию ESP после OTA в этой задаче не подтверждали.

## Названия главных кнопок — Android build 257
- PASS: baseline 256 и итоговая 257 успешно собраны build-android.ps1, итоговая сборка 46с.
- PASS: APK установлен и запущен на физическом телефоне; UI hierarchy содержит все три новых названия и «Сборка 257».
- PASS: скобки в строковом ресурсе firmware_update_available, только подпись получает Color.BLACK и RelativeSizeSpan(0.6).
- PASS: accelerometer_rotation/user_rotation сохранены перед установкой, восстановлены и проверены после установки/запуска, оба 0.
- Unit tests повторно не запускались: изменение ограничено текстом и цветом. Runtime подпись обновления сейчас скрыта, поскольку downloaded и sent совпадают; её цвет проверен по коду.
- Сведения сохранены в production/verification/button-names-257/, актуальный APK — production/android-debug/.

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


## Android 304 — проверка запуска обновлений ESP, 10.10.2026
PASS: исходная release 303 и итоговая release 304 успешно собраны build-android.ps1 -Action release, включая R8/lintVital. build-android.ps1 -Action test успешен: debug 62/62 и release 62/62, без failures/errors/skips, включая FirmwareUpdateClient/FirmwareTransferState. Проверка исходного onResume подтвердила Handler.post без задержки; running guard не создаёт повторный запрос, completion удаляет лишние callbacks.
APK проверен verify_apk и установлен поверх 301 по Wi-Fi 192.168.1.65:33719, Success, versionCode=304/versionName=1.0.304 подтверждены. accelerometer_rotation/user_rotation сохранены перед установкой, восстановлены и проверены 0/0 (.device-agent/firmware-check-304-orientation.json). Главный экран после установки и повторного HOME→открытия отображает сборку 304 и пометку «(доступно обновление)» на настройках замка, UIAutomator .device-agent/firmware-check-304-open.xml. Из этого UI-снимка нельзя отдельно определить момент HTTP-запроса или номер уже сохранённого образа; немедленный запуск подтверждён кодом lifecycle. Команды открытия/OTA на замок не отправлялись.
Публикация app-v1.0.304 успешна: SHA-256 b5b21b185e965572dafaf6f597232e40dade1a21979415531578d69bc99ee830; предыдущий APP 301 удалён, ESP 461 сохранён. Комплект production/android-release-304/.


## Android 306 — проверка подписи брокера, 10.10.2026
PASS: исходная release 305 до правок и итоговая release 306 успешно собраны build-android.ps1 -Action release, включая R8 и lintVital. Источник host в кнопке и InternetOpen.start сверены: оба читают InternetAccess.load(). Статическое чтение подтвердило listener изменения sealed, attach/detach и отсутствие изменения стадий. Дополнительные тесты для замены подписи не создавались; git diff --check успешен.
APK проверен verify_apk, подписан постоянным release certificate SHA-256 be9a249131a80bfe499c35dbad0752a13be5c4d02e4198b9da436e30d59885c0. Установка по Wi-Fi 192.168.1.65:33719 успешна, dumpsys подтвердил versionCode=306/versionName=1.0.306. accelerometer_rotation/user_rotation сохранены перед установкой и восстановлены с повторной проверкой 0/0; .device-agent/broker-label-306-orientation.json.
Телефон заблокирован: после запуска и пробуждения UIAutomator показывает lock screen, визуальный вид надписи на физическом устройстве не подтверждён. Интернет-команды открытия не отправлялись, настройки MQTT не менялись. ESP не изменялся.

APP 306 опубликован: SHA-256 eea3422a5f020c48c63d43bfecd1b5ba23a4f7402183530dd27b2ef67221a34c. Публичные APK/metadata проверены publisher, APP 304 удалён, ESP global latest сохранён. Комплект production/android-release-306/.

Проверка пользователем: 10.10.2026 пользователь подтвердил, что отображение текущего брокера в кнопке интернет-открытия работает. Приложение 306 и прошивка 480 подготовлены к отдельным коммитам; код после успешных сборок не изменялся.
## 1.0.308 — аудит APK, 10.10.2026

PASS: baseline release 307, итоговая release 308 и build-android.ps1 -Action test.
68 тестов debug и 68 release без ошибок; FirmwareUpdateClient 11 сценариев, 6 новых.
Чужой redirect отклоняется до подключения; GET/HEAD, HTTPS downgrade, userInfo/порт,
другой repo, петля, отсутствие Location и транспортные ошибки проверены.
Release отвергает loopback; debug не следует loopback redirects.
apksigner/aapt/aapt2/dexdump: com.blelock.app 1.0.308, debuggable=false, прежняя подпись;
новые backup ресурсы вошли в APK (9 старых и 18 cloud/D2D исключений).
Android Log invoke — 0; R8 переименовал/объединил проверенные классы.
Закрытых ключей/файлов подписи и PEM private-key маркеров не обнаружено.
Размер 472768, SHA-256 2e3833f0527471d3c07c86f58c3629f4b05210472a9340f109cbf3c14a890d63.
Буферные finally проверены по коду; физические ошибки Keystore/D2D/root/hooking не тестировались.
Локальный APK production/android-release-308/thekidlock.apk, отчёт .device-agent/android-security-308.json.
Установка, публикация и изменения ESP не выполнялись. Ограничения изложены в корневом AUDIT.md.
## 1.0.310 — проверка переводов, этап 1
Baseline 309 и итоговая 310 успешно собраны. 71 debug + 71 release тестов успешны;
проверены паритет ресурсов, аргументы формата, arrays, fallback и localeConfig.
Физический телефон подключён, но заблокирован; визуальная проверка не выполнена.
## 1.0.311 — итоговые проверки
PASS: project release build, R8/lintVital, 71 debug + 71 release тестов.
PASS: ключи/array lengths/formats четырёх source sets, English default без кириллицы;
13 активных строк default/en/ru/uk и localeConfig подтверждены в фактическом APK.
PASS: подпись постоянная, debuggable=false, Android Log invoke=0, backup/D2D исключения сохранены.
Размер 534752 байт, SHA-256 9f75a45457b49df421e3b9734040496b47aaa03e5e506ab6bc5f5f430e3eee5a.
На телефон не установлено: чтение исходной ориентации через ADB shell отказало до install.
Ориентация/язык не изменены, visual/UI и configuration change на физическом устройстве
не проверены. Отчёты .device-agent/localization-apk-311.json и android-security-311.json.
APK production/android-release-311/thekidlock.apk, публикация не выполнялась.
## 1.0.312 — установлена на телефон
PASS: release сборка, 71+71 unit-тестов и фактический APK locale/R8/подпись/backup checks.
Размер 535264 байт, SHA-256 3712c8a8d393ba264996b3c9050f2b1f2929972ad938e651b129af4837e66171.
PASS: Wi-Fi ADB install, versionCode=312/versionName=1.0.312 подтверждены.
Android принял en/uk/ru, английское уведомление подтверждено dumpsys; uk/ru live notification
отсутствовало. Исходный app locale [] и accelerometer_rotation/user_rotation 0/0 восстановлены.
Визуальная проверка ограничена keyguard. Список файлов в CHANGELOG.md, доказательства
.device-agent/localization-install-312.json, localization-apk-312.json, android-security-312.json.
На GitHub не опубликовано, ESP не изменена.

## Android 316 — экран параметров и язык

Базовая сборка 314 до изменений: PASS (build-android.ps1 -Action release). Сборка 316 после изменений: PASS (build-android.ps1 -Action release). Ручная проверка на телефоне не выполнялась; APK не устанавливался, ориентация не менялась.

Итог 316: PASS — release package, R8 и lintVital завершились успешно. versionName=1.0.316, APK 540188 байт, SHA-256 F1164E47187A474DB132CFACEF745B54B88306930D9B2EC2FFE892151547D54A. git diff --check PASS. Unit tests и физическая установка не выполнялись, визуальная проверка меню ожидает запуска на устройстве. GitHub не опубликовано.

Физическая установка 316: PASS через ADB Wi-Fi 192.168.1.65:33719 после переподключения устройства. package dump подтвердил versionCode=316/versionName=1.0.316. accelerometer_rotation/user_rotation сохранены 0/0 перед install, восстановлены и перепроверены после. UI вручную не запускался и визуально не проверен.

## Android 318 — значки кнопки настроек замка

Базовая release-сборка 317 перед изменением прошла. Финальная сборка ожидается; аппаратная проверка ожидает установки APK.

Итог 318: release сборка PASS. Установка на 192.168.1.65:33719 PASS; подтверждена versionCode=318/versionName=1.0.318. До установки сохранены значения ориентации 0/0, после восстановлены и проверены 0/0. Ручной запуск и визуальная проверка не выполнялись.

Итог Android Web UI JSON localization: PASS release build-android.ps1 -Action release, versionCode 320; APK установлен на устройство 192.168.1.65:33719. Перед установкой и после неё accelerometer_rotation/user_rotation прочитаны как 0/0. Приложение не запускалось, поэтому экран WebView на телефоне визуально не проверен. Валидация словарей подтверждена отдельной проверкой: en/ru/uk по 125 одинаковым ключам, требуемые ключи HTML присутствуют.

## Web UI localization baseline

PASS до изменений: build-android.ps1 -Action release. Финальный результат ожидается; JSON-ресурсы будут проверены Android-сборкой.

## Android 323 — симулятор замка

PASS: `build-android.ps1 -Action test` — 71 debug + 71 release unit tests.
PASS: debug build и release build; release APK 1.0.323, 548305 байт, SHA-256
FC660BAC7BBC3CF19CFDC2A7382288E34DA4E51F6D041A625161AB98F3921207.
PASS: установка release APK через Wi-Fi ADB поверх версии 320; подтверждены
versionCode=323/versionName=1.0.323. Значения accelerometer_rotation/user_rotation
сохранены 0/0, восстановлены и повторно проверены 0/0. Debug APK поверх release
не установлен из-за несовпадения подписи; приложение не удалялось.
Физический экран оставался заблокирован, визуальная проверка UI не выполнена.

## Android 325 — виртуальный замок

PASS: 71 debug + 71 release unit tests; release build 1.0.325 создан и установлен.
SHA-256 `DC03A1E73E82C8A962FC9251F8AF5F0212FDCC0BCE04C05E74C7164BD0C8153E`.
Ориентация до/после установки: 0/0. ADB стало offline после запуска, поэтому ручная
проверка переключателя и главного экрана на устройстве не выполнена.

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

Публикация ESP 486 проверена: файл latest по закреплённой ссылке имеет SHA-256 288fc309567d19fa22ac2752a9b6699d4b9cd0e1d7108490f70b221fad39ab61, совпадающий с локальным OTA-образом.

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

## 11.10.2026 — панель дашборда

Baseline Release 1.0.349 и финальная Release 1.0.350 собраны проектным скриптом; R8/lintVital успешны. APK 1.0.350 установлен на устройство 192.168.1.65:33719. До/после установки и запуска: `accelerometer_rotation=1`, `user_rotation=0`. UI hierarchy физического устройства подтверждает отдельную панель над автооткрытием и актуальные значения: замок заперт, дверь приоткрыта, 3,93 В. MQTT отображение проверено на действующем замке. Gate для не-владельца и случая без домашнего Wi-Fi проверен статически, отдельное аппаратное воспроизведение не проводилось.

## Android Release 1.0.352 — согласование дашборда

Baseline Release 1.0.351 успешно собрана до правок. Release 1.0.352 прошла R8/lintVital и установлена на физический телефон. На экране отображается «Замок заперт, Дверь закрыта» при подтверждённом `lock=locked`; дверь нормализуется и для retained state старой версии ESP. Открытая дужка нарисована с зазором между концом и корпусом. `accelerometer_rotation=1`, `user_rotation=0` сохранены и проверены после установки и запуска.

## 11.10.2026 — публикация Android 1.0.355

PASS: `publish_android_release.py` опубликовал `app-v1.0.355`, проверил подпись,
публичный APK SHA-256 и manifest. Удалён предыдущий app release `app-v1.0.306`.

## Android — интернет-кнопка только владельцу, build 357
Baseline debug 356 собран успешно до правки. Release-сборка 357 прошла; runtime проверка скрытия на двух ролях на устройстве не выполнялась.

Результат: `build-android.ps1 -Action release` успешно собрал подписанный APK build 357. Установить не удалось: целевой ADB Wi-Fi адрес 192.168.1.65:33719 не подключён, `adb devices -l` не показывает устройств. До попытки установки не доходило, значения ориентации не менялись. Runtime проверка роли на устройстве не выполнялась.

## Публикация Android 1.0.357 — 11.10.2026
PASS: release опубликован publisher-скриптом. Постоянная подпись совпала; публичная загрузка APK и update manifest сверена с локальными данными, APK SHA-256 `86c41316fdef964acb67720477f189efb6d5a4047902de901039094ba935d168`. Предыдущий Android-релиз 1.0.355 удалён. Установка на устройство не выполнялась, ADB недоступен.
## Build 359 — удаление дашборда — 11.10.2026

Исходная debug-сборка 358, итоговая release-сборка 359 и все debug/release
unit-тесты прошли. Специальных ссылок на панель состояния и её BLE-запрос
в исходниках не осталось. Проверка на телефоне и установка не выполнены:
ADB `192.168.1.65:33719` недоступен; ориентация телефона не изменялась.
## Публикация build 359 — 11.10.2026

PASS: `publish-android.ps1` опубликовал `app-v1.0.359`, сверил подпись и
публичные APK/JSON, затем удалил `app-v1.0.357`. Повторное чтение GitHub API
подтвердило два файла релиза и сохранение ESP как global latest. SHA-256 APK:
`6c457eeea7b97018af5847704fba7b1099d9f763fe4249e2f69372098e889cd4`.
## Build 361 — значок и размер виджета — 11.10.2026

Исходная debug-сборка 360 и итоговая release-сборка 361 успешны.
Копия `image_1.png` побайтово соответствует исходнику. У виджета сохранены
старые изображения двух состояний, `resizeMode` разрешает оба направления,
лимит 60 dp убран. Устройство по ADB недоступно, поэтому установка и
визуальная проверка изменения размера не выполнены.
## Публикация build 361 — 11.10.2026

PASS: `publish-android.ps1` выпустил `app-v1.0.361`, проверил публичные
APK/JSON и постоянную подпись. API GitHub подтвердил оба файла релиза;
старый app 359 удалён, ESP global latest сохранён. SHA-256 APK:
`78e95ed7d8674a78588f5a8970315cac2bf3b48acad9ce7f1d8199c169c8f194`.
## Build 363 — оформление виджета — 11.10.2026

Исходная debug-сборка 362 и подписанная release-сборка 363 прошли.
Метка ярлыка и её ресурс удалены; закрытый и открытый значки имеют общий
тёмный фон. ADB-устройство недоступно, поэтому проверка на рабочем столе
телефона и установка не выполнены.
## Публикация build 363 — 11.10.2026

PASS: `publish-android.ps1` опубликовал `app-v1.0.363`, сверил подпись
и публичные APK/JSON, удалил предыдущий app-релиз 361. Независимый
запрос GitHub API подтвердил оба файла и сохранение ESP global latest.
SHA-256 APK: `f9d3a020477d9d15a809ec329e429cfc6b11e8ca27fb2a3a5504b95b646523a6`.

## Локализация — 11.10.2026
- PASS: проектный `build-android.ps1 -Action test`; debug и release unit tests, 69 тестов.
- PASS: проверены полнота Android-ключей, плейсхолдеры, список системных локалей и схемы/плейсхолдеры JSON встроенного Web UI.
- PASS: все новые XML-ресурсы корректно разбираются.
- Следующий шаг: release-сборка для получения устанавливаемого APK.
- PASS: release-сборка `build-android.ps1 -Action release`, версия 1.0.365.
- PASS: подпись APK проверена `apksigner`; SHA-256 `4142037088333D68D9700BCDCE5EB236D3CD2B4F722FD16F8199B31653A933B8`.
- NOT INSTALLED: ADB-устройство не обнаружено; подключение `192.168.1.65:33719` отклонено. Системные параметры ориентации не менялись.
- PASS: повторный `build-android.ps1 -Action test` после добавления локалей озвучки; debug/release tests успешны, 69 тестов.
- PASS: `publish-android.ps1` опубликовал `app-v1.0.366`, сверил подпись, публичные APK и metadata; SHA-256 `15c89bdb9fbec9444f0df842e6766ceeba28702a915b1fb50700934e9e78aef6`. Предыдущий Android-релиз 1.0.363 удалён, ESP latest не затронут.
- APK 1.0.366 опубликован; установка на устройство будет проверена отдельно, если ADB доступен.
- Установка APK 1.0.366 не выполнена: `adb devices -l` пуст, подключение к `192.168.1.65:33719` отклонено. Параметры ориентации не менялись и не требовали восстановления.

## Полная локализация мастера калибровки — 11.10.2026
- PASS до изменений: `build-android.ps1 -Action test`.
- PASS после перевода: `build-android.ps1 -Action test`; debug/release unit tests прошли, включая все ключи мастера и placeholder args.
- Проверена согласованность JSON locale и TTS language tags для pl/be/lt/lv/de/ja.
- Следующий шаг: release APK и публикация GitHub.
- PASS: release 1.0.367 опубликован штатным publisher'ом; публичные APK и update metadata сверены, SHA-256 `90d3274e0dccc7b2bc235c5f6e4004ec9361852faa1d847e5fae104e73cb266a`. Предыдущий app-релиз 1.0.366 удалён, ESP-релиз сохранён.
- NOT INSTALLED: ADB-устройство отсутствует; подключение `192.168.1.65:33719` отклонено. Ориентация устройства не менялась.
# 12.10.2026 — удалённый отзыв ключей

- До изменений `build-android.ps1 -Action test` успешно собрал debug/release и выполнил unit tests.
- После изменений `build-android.ps1 -Action test` успешно собрал debug/release и выполнил unit tests; полнота локалей проверена существующим тестом.
- ADB не нашёл подключённого телефона; подключение к `192.168.1.65:33719` завершилось тайм-аутом. Установка APK не выполнена, настройки ориентации не менялись.
- Подписанный release APK 1.0.372 успешно собран с lintVital. Публикация на GitHub прошла; скрипт проверил хэш публичного APK и метаданные обновления.
- Изменены: `app/src/main/java/com/blelock/app/MainActivity.kt`, новый `RemoteKeys.kt`, `app/src/main/res/values*/strings.xml`, `CHANGELOG.md`, `TEST_REPORT.md`.
# 12.10.2026 — диагностика удалённого отзыва

- Исходная сборка Android debug/release tests 1.0.372 прошла до изменений.
- После добавления меток `build-android.ps1 -Action test` прошёл; debug/release компилируются, unit tests успешны.
- Изменены `app/src/main/java/com/blelock/app/RemoteKeys.kt`, `CHANGELOG.md`, `TEST_REPORT.md`.

- PASS: Android debug/release tests после замены временного `System.err` вывода на адресную метку `Log.println(ASSERT, ...)`.
- Требуется установить APK, сохранить и восстановить `accelerometer_rotation` и `user_rotation`, затем повторить тап и проверить маркеры RemoteKeys.

- ESP build 10.0.521 загружен по OTA после успешной signed-dev сборки; возврат версии подтверждён Agent API.

# 12.10.2026 — иконка и размеры кнопок главного экрана

- PASS: baseline Android debug build перед изменениями.
- PASS: `build-android.ps1 -Action test` — успешны debug/release compile и unit tests.
- PASS: `build-android.ps1 -Action release` — APK 1.0.387 собран.
- NOT INSTALLED: `adb devices` не обнаружил подключённый телефон; значения ориентации не менялись.
- Установка APK 1.0.387 через ADB `192.168.1.65:38489` успешна; Package Manager подтвердил `versionName=1.0.387`. Параметры ориентации `0/0` сохранены и восстановлены без изменений.

# 12.10.2026 — экран настроек приложения и отзыв ключей

- PASS: baseline Android build перед изменениями.
- PASS: Android tests, release build APK 1.0.389 и установка по ADB.
- PASS: параметры ориентации сохранены до установки и восстановлены после (`0/0`).

# 12.10.2026 — экран настроек приложения

- PASS Android baseline debug build, debug/release tests, release APK 1.0.389.
- PASS установка APK 1.0.389 через ADB; настройки ориентации `0/0` сохранены и восстановлены.

# 12.10.2026 — адаптивная высота и таймер интернет-открытия

- PASS: `build-android.ps1 -Action test` — debug/release compilation и Android unit tests.
- Визуальная установка APK и проверка фактического таймера выполняются после release сборки и OTA новой прошивки.

- PASS: release APK 1.0.391 собран, установлен на телефон; системные значения ориентации сохранены и восстановлены (`0/0`).

- PASS: после Android тестов собран ESP signed-dev build 10.0.554, подпись и encrypted OTA round-trip подтверждены.
- PASS: OTA Device Agent обновила замок 10.0.552 → 10.0.554.

- PASS unit tests покрывают HMAC-подпись close_remaining_ms и округление обратного таймера вверх до секунд.

# 12.10.2026 — единая кнопка автооткрытия

- PASS baseline debug build; после изменения PASS `build-android.ps1 -Action test` для debug/release и unit tests.
- Статусы кнопки автооткрытия добавлены во все Android-локали.

- PASS release APK 1.0.393 установлен; системная ориентация `0/0` восстановлена и проверена.
- PASS UI Automator: пять видимых кнопок главного экрана и кнопка системных настроек приложения имеют равную высоту 277 px.
- Изменены `MainActivity.kt`, `bg_handoff_card_active.xml`, `ic_handoff_check.xml`, `values*/strings.xml`, `CHANGELOG.md`, `TEST_REPORT.md`.

# 12.10.2026 — виброотклик кнопок и выравнивание настроек

- PASS: исходная debug-сборка и итоговые debug/release unit tests проектным скриптом.
- PASS: подписанный release APK 1.0.395 собран и установлен через ADB на `192.168.1.65:38489`; системные параметры ориентации до и после установки `0/0`.
- PASS UI Automator: языковые RadioButton, кнопка системных настроек приложения и ссылка на проект имеют одинаковые горизонтальные границы `[144,936]` на текущем телефоне. В системных настройках телефона тактильный отклик разрешён (`haptic_feedback_enabled=1`).
- Изменены `ButtonHaptics.kt`, `MainActivity.kt`, `SettingsUi.kt`, `BUILD_NUMBER`, `CHANGELOG.md`, `TEST_REPORT.md`.

# 12.10.2026 — публикация Android 1.0.395

- PASS: release `app-v1.0.395` опубликован; публичные APK и update metadata сверены publisher. SHA-256 APK `9b2dd8402d364a50b499461fe94286ad683fac8f921309db2914c22923de0da6`, сертификат постоянной подписи совпадает.
- PASS: в описании релиза опубликованы прямые ссылки на APK и update metadata; Android 1.0.385 удалён, ESP global latest сохранён.
