# Журнал текущей версии

## 10.0.436 — 10.10.2026

Рабочий OTA профиль finalize-existing для защищённого ESP32-S3 с NVS HMAC KEY4.
После успешной проверки NVS/ULP/BLE и валидной калибровки приложение подтверждает
OTA, затем необратимо отключает ROM Download. Операция выполнена на текущем
замке по отдельному разрешению. Калибровка и настройки сохранились.

Encrypted OTA опубликован в uah0/thekidlock, latest esp-v10.0.436.
Publisher поддерживает finalize-existing с обязательными authorization flags,
проверкой профиля, подписи, decrypt round-trip, compiled version и public hash.

## Очистка проекта — 10.10.2026

Удалены из рабочей папки все ESP build* каталоги, Gradle/Android build caches,
artifacts/, log/, временные тестовые папки .device-agent, диагностические BLE
документы/патч и старые ESP/Android/ULP audit отчёты. Файлы находятся в Корзине.
Текущий комплект и подтверждения сохранены в production/. .security/ сохранена.

Изменены .gitignore, AUDIT.md, CHANGELOG.md, TEST_REPORT.md,
docs/FACTORY_SECURITY_PLAN.md, Android CHANGELOG.md/TEST_REPORT.md и README.md.
Добавлены production/ комплект и инвентаризация; runtime исходники не менялись.
Кэши будут созданы заново при следующей сборке. Дальнейшая проверка bootstrap
нового замка описана в docs/FACTORY_BOOTSTRAP_PLAN.md и пока не реализована.


## Организация документации — 10.10.2026
Восемь технических документов перенесены из корня в docs/; MQTT_DESIGN.md переименован в MQTT_PROTOCOL.md. Создан docs/BLE_GATT.md с актуальным контрактом из старого SERVICE_BEACON_PLAN.md; старый план убран в Корзину. Обновлены ссылки README.md, docs/MQTT_SETUP.md и основных отчётов. Прошивка/Android код не менялись.

## OTA только с телефона — ESP 10.0.438
Прямой HTTPS downloader из wifi_ota.c удалён; legacy start/check возвращают ESP_ERR_NOT_SUPPORTED. Старый auto_update в NVS не запускает загрузку, getter всегда false; попытка включения через /api/wifi/auto-update возвращает HTTP 400 с пояснением. Исправлен early return PHONE_ONLY_HANDLER, сохраняются Wi-Fi/MQTT, приём encrypted POST /api/ota, подпись, rollback, ULP/BLE и алгоритм замка. Android уже реализует HEAD/GET → private AtomicFile → уведомление Toast → ручная кнопка Web UI; его исходники не менялись. Изменены main/wifi_ota.c, main/wifi_ota.h, main/web_ui.c, AUDIT.md, CHANGELOG.md, TEST_REPORT.md. Итоговая сборка успешна; runtime-проверка ожидается.

10.0.438 установлена штатной кнопкой через хранилище телефона и подтверждена runtime. Опубликована как latest esp-v10.0.438; public encrypted asset 1 380 864 байта, SHA-256 e31a163106c02fed5080fd04b2f95d3a757c90bf8b5a0d3e42c511af015604ca. Обновлены README.md, docs/FACTORY_SECURITY_PLAN.md и docs/FACTORY_BOOTSTRAP_PLAN.md под получение только с телефона; комплект сохраняется в production/esp-10.0.438/.

После завершения восстановлена чистая структура: временные сборки/кэши/helpers убраны в Корзину, production содержит текущий комплект 438. Android APK в production/android-debug не заменялся: установленное приложение осталось без изменений.

## Интернет-открытие только владельцем — 10.0.440
ESP проверяет MQTT phone ID по сохранённому BLE владельцу. Команды других bonded телефонов отклоняются до NVS/открытия, данные для MQTT экспортируются только владельцу. Отсутствующая/недоступная/некорректная owner запись означает отказ. BLE открытие остальных телефонов, таймеры и формат NVS сохранены.
Изменены main/ble_phone.c, main/ble_phone.h, main/cloud_commands.c, tools/test-cloud-auth.py, BUILD_NUMBER, AUDIT.md, CHANGELOG.md, TEST_REPORT.md. Комплект production/esp-10.0.440. Установку пользователь выполняет самостоятельно, GitHub latest остаётся 438.

По отдельному указанию опубликована 10.0.440: latest esp-v10.0.440, единственный asset encrypted ble_lock_stage1_s3.bin. Публичная загрузка совпала по SHA-256. Установку пользователь выполняет сам.

## Индикатор обновления Android — build 253
Скачанный encrypted OTA хранится с номером GitHub release; загрузка закреплена за конкретным тегом, чтобы номер соответствовал файлу. Для каждого выбранного BLE-замка номер отмечается при начале POST /api/ota, без ожидания ESP и без отмены при ошибке передачи. В кнопке Настройки отображается меньшим шрифтом «доступно обновление», если скачанный номер отличается от отмеченного. Добавлен JS bridge в существующую WebView, прошивка ESP не менялась. Старый cache без номера автоматически скачивается заново.
Изменены FirmwareUpdateClient.kt, LockWebUiDialog.kt, MainActivity.kt, strings.xml; добавлены FirmwareTransferState.kt, assets/firmware_transfer.js, FirmwareTransferStateTest.kt. Обновлены BUILD_NUMBER Android и отчёты AUDIT/CHANGELOG/TEST_REPORT. APK установлен на физический телефон; orientation 0/0 сохранена и восстановлена. Телефон автоматически скачал 10.0.440 с GitHub, SHA-256 совпал. OTA на ESP не выполнялась.

Итоговая Android сборка 255: идентификатор для режима PhoneBeacon получаем из существующего ble_peer; надпись обновляется при BLE controls. Расширен WebUiOtaInstrumentation для проверки сохранения номера на локальном стенде; временные данные восстанавливаются до завершения runner. Обновлённый APK 255 установлен, OTA на ESP агент не выполнял. Подтверждена реальная отметка начала пользовательской передачи 10.0.440.

## Названия главных кнопок — Android build 257
«Автоматический режим» заменён на «Автооткрытие замка», «Замок постоянно открыт» — на «Оставить замок открытым», «Настройки» — на «Настройки замка». Подпись теперь «(доступно обновление)», чёрная через ForegroundColorSpan; меньший размер 0.6 сохранён. Изменены app/src/main/res/values/strings.xml и MainActivity.kt, Android BUILD_NUMBER и отчёты. Логика замка/OTA не менялась. Итоговый APK установлен на физический телефон.

## Упрощение Web UI — ESP 10.0.442
Из main/web/index.html удалены переключатель «Выводить логи в монитор последовательного порта», saveSerialLog и ссылка на удалённый checkbox в loadConfig. Убрано idle пояснение «Будет установлена прошивка, сохранённая в приложении» под OTA кнопкой; сообщения передачи/ошибок/результата сохранены. Флаг NVS, API и механизм журналов не изменялись. Изменены main/web/index.html, BUILD_NUMBER, AUDIT.md, CHANGELOG.md, TEST_REPORT.md. Собрана и опубликована latest esp-v10.0.442 для самостоятельной установки пользователем. Комплект production/esp-10.0.442/.

## Только последний GitHub релиз — 10.10.2026
В uah0/thekidlock оставлены только esp-v10.0.442 и его тег. Удалены 11 остальных release записей с assets, включая 2 пустых drafts, и 10 старых esp-v tags. tools/publish_esp32_release.py после успешной проверки публичного OTA автоматически удаляет предыдущие релизы/теги. Проверяет ID текущего latest перед удалениями, обрабатывает пагинацию и отказывает при неожиданной новой версии. Существующий draft текущего тега переиспользуется вместо создания дубликата. Добавлен tools/test-release-retention.py, актуализированы AUDIT.md, CHANGELOG.md, TEST_REPORT.md и README.md. ESP/Android исходники не менялись.

## Окно регистрации в кнопке — ESP 10.0.444
Кнопка Web UI во время регистрации показывает «Регистрация телефона · N с», используя оставшееся время ESP из /api/ble/registration и существующее обновление раз в секунду. После закрытия окна возвращается «Разрешить регистрацию телефона». Удалены отдельная строка состояния над кнопкой и пояснение под ней, а также неиспользуемый CSS pairing-state. При потере связи кнопка сообщает «Регистрация: нет связи». BLE регистрация/API/60-секундное окно не менялись. Изменены main/web/index.html, BUILD_NUMBER и AUDIT/CHANGELOG/TEST_REPORT. Собран и опубликован latest esp-v10.0.444, прежний релиз удалён автоматически. Установку пользователь выполняет сам.

## Главный экран TheKidLock по архиву handoff — 10.10.2026
Нативные Views сохранены. Добавлены шестерёнка системных настроек приложения, карточка автооткрытия с реальным переключателем режима BLE-маяка телефона, существующий дискретный TX-power slider с крайними подписями, командные карточки с иконками и подписями, стрелка только у настроек замка. Footer использует фактическую высоту и реальные BuildConfig данные. Названия пользователя и чёрный индикатор «(доступно обновление)» сохранены вопреки устаревшим текстам макета. Постоянная подсветка команды удержания убрана, существующий BLE-обработчик toggleKeepOpenMode сохранён; протокол и службы не изменены.
Изменённые файлы: MainActivity.kt, InternetOpenProgressButton.kt, strings.xml, colors.xml, bg_handoff_card.xml, ic_handoff_settings.xml, ic_handoff_unlock.xml, ic_handoff_bluetooth.xml, ic_handoff_chevron.xml, ic_handoff_cloud.xml, Android BUILD_NUMBER; AUDIT.md, корневые и Android CHANGELOG.md/TEST_REPORT.md.
Итоговая версия Android — build 261, APK установлен. Исходное состояние приложения до изменения экрана сохранено коммитом 8e324b5.

## Пять подписей интернет-открытия — Android 265 / ESP 446
На кнопке только: «Команда отправлена», «Облако приняло команду», «Замок разблокирован», «Дверь открылась», «Дверь закрыта и заперта». Первый этап показывается сразу после принятия нажатия приложением; отправка MQTT и PUBACK разделены через существующий Paho delivery token (QoS 1). Открытие двери — только по подписанному door=open; closing/ошибка не добавляют новых подписей и сохраняют последний подтверждённый этап. Итог показывается 3 секунды. ESP добавляет существующее совместимое поле door в HMAC; управление замком, NVS record и BLE протокол не изменены.
Изменённые файлы этого этапа: InternetOpen.kt, InternetOpenProgressButton.kt, MainActivity.kt, strings.xml, InternetOpenProgressButtonTest.kt, Android BUILD_NUMBER; main/cloud_commands.c, main/lock_control.c, main/lock_control.h, ESP BUILD_NUMBER; tools/test-cloud-auth.py; AUDIT.md и корневые/Android CHANGELOG.md, TEST_REPORT.md.
ESP 10.0.446 опубликована: https://github.com/uah0/thekidlock/releases/tag/esp-v10.0.446 . Предыдущий релиз 444 и его тег удалены; остался только последний. Установка ESP выполняется пользователем с телефона.

## Диагностика отсутствия «Дверь открылась» — 10.10.2026
В private firmware_transfer телефона downloaded_version=10.0.446, последняя отмеченная передача текущему замку sent=10.0.444. Прошивка 444 публикует шесть полей без door; новый Android не подменяет closing подтверждением открытия двери. Данные для этого этапа добавлены в уже опубликованной ESP446, требуется пользовательская установка этой версии. Фактическая версия ESP не подтверждена: сохранённый адрес 192.168.1.54 недоступен. Изменений функционального кода не выполнено. Диагностические baseline Android266 и ESP447 успешно собраны, не устанавливались и не публиковались; рабочий APK265 и публичная прошивка446 сохранены. Изменены автоматически только BUILD_NUMBER/Android BUILD_NUMBER и отчёты.

## Название устройства в интерфейсе — Android268 / прошивка449
ESP32/ESP заменены на «замок» в пользовательских надписях Android: версии прошивки, ожидании, уведомлениях Wi-Fi, BLE ошибках и диагностике. В прошивке заменены заголовок устройства, перезагрузка, описание RSSI, сообщение подтверждения, ожидание калибровки и стартовый журнал. Согласованы род и падежи. Технические device_type/resource ID/методы/SDK обозначения сохранены.
Изменённые файлы: app/src/main/res/values/strings.xml; Android MainActivity.kt, BleLockService.kt, BleRepository.kt, PhoneBeacon.kt, WifiLockControl.kt, FirmwareUpdateClient.kt; main/web/index.html, main/web/calibration.html, main/main.c; BUILD_NUMBER, android_lock_beacon/BUILD_NUMBER; AUDIT.md и корневые/Android CHANGELOG.md/TEST_REPORT.md.
Прошивка449 опубликована для установки пользователем; прежний релиз446 и тег удалены. Android APK268 готов: production/android-debug/app-debug.apk. Установка на телефон отклонена Android (INSTALL_FAILED_USER_RESTRICTED: Install canceled by user), повторные попытки не выполнялись.

Повторная установка по указанию пользователя: APK268 успешно установлен через ADB --no-streaming (Success), MainActivity запущена. accelerometer_rotation/user_rotation перед установкой сохранены, после установки/запуска восстановлены и проверены: 0/0. Предыдущее ограничение установки снято.

## Web UI по index_updated.html и NVS default звука — прошивка451
Убраны advanced toggle, SYS панель и подробная ULP диагностика. Настройки замка сразу доступны; ползунок «Зона открытия» отображает 30..127 и сохраняет -30..-127 в прежнем RSSI API. ТаймерА переименован, алгоритм сохранён. TestOpen и пять показателей перенесены вниз CFG. Переключатель «Включение звукового подтверждения открытой двери» сохраняет прямую логику enabled. При отсутствии ключа blelock/departure_beep app_config_init один раз записывает1 и commit; существующие значения0/1 не перезаписывает. Ошибки NVS возвращаются вызывающему коду.
Изменённые файлы: main/web/index.html, main/app_config.c, tools/test-app-config-defaults.py, BUILD_NUMBER, AUDIT.md, CHANGELOG.md, TEST_REPORT.md. Исходный index_updated.html пользователя сохранён без изменений. Android не изменялся. Последний публичный релиз451; предыдущий449 и тег удалены. Установка выполняется пользователем через телефон.

## Android 270 — виджет автооткрытия, 10.10.2026
Добавлен виджет «Автооткрытие замка» для главного экрана. Нажатие открывает MainActivity и включает существующий режим BLE-маяка; повторное нажатие не выключает режим. Разрешения запрашиваются существующим обработчиком. Все экземпляры виджета показывают сохранённое состояние и обновляются при изменении переключателя в приложении.
Изменены: app/src/main/AndroidManifest.xml, MainActivity.kt, LockSettings.kt, res/values/strings.xml; добавлены AutoOpenWidget.kt, res/layout/widget_auto_open.xml, res/xml/widget_auto_open_info.xml. Номер Android-сборки повышен проектным скриптом до 270; обновлены AUDIT.md, CHANGELOG.md, TEST_REPORT.md и Android-отчёты. Прошивка не изменялась.

## Android 272 — добавление виджета из приложения, 10.10.2026
В карточку автооткрытия добавлена кнопка «Добавить виджет на главный экран». Используется стандартный AppWidgetManager.requestPinAppWidget, проверка поддержки и сообщение при отказе. Метаданные дополнены размером сетки 2×2 и previewImage для старых каталогов. Добавление не меняет autoOpenEnabled.
Изменены: MainActivity.kt, res/values/strings.xml, res/xml/widget_auto_open_info.xml, BUILD_NUMBER Android; обновлены корневые AUDIT.md/CHANGELOG.md/TEST_REPORT.md и Android CHANGELOG.md/TEST_REPORT.md.

## Android 274 — фоновое переключение автооткрытия виджетом, 10.10.2026
Нажатие виджета теперь использует PendingIntent.getForegroundService и действие BleLockService.ACTION_AUTO_OPEN_TOGGLE: переключает сохранённый режим через существующий setAutomaticModeInternal без запуска MainActivity. Удалена обработка старого действия открытия Activity. Виджет показывает «Включено · выключить» / «Выключено · включить». При отсутствии BLE-разрешений показано сообщение, настройка не включается. BootReceiver обновляет PendingIntent размещённых виджетов после обновления приложения, broadcast режима синхронизирует переключатель открытого экрана.
Изменены: AutoOpenWidget.kt, BleLockService.kt, MainActivity.kt, BootReceiver.kt, res/values/strings.xml и Android BUILD_NUMBER. Обновлены AUDIT.md, корневые и Android CHANGELOG.md/TEST_REPORT.md. Прошивка не изменена.

## Android 277 — компактное добавление виджета, 10.10.2026
Большая текстовая кнопка добавления виджета убрана из карточки автооткрытия. Под последней основной кнопкой справа размещена компактная кнопка без текста, с рисунком капсулы и круглой части со знаком «+». Действие requestPinAppWidget сохранено; текст назначения доступен через contentDescription. Остановленные изменения регулятора мощности не выполнялись.
Изменены: MainActivity.kt, Android BUILD_NUMBER; добавлен res/drawable/ic_widget_add_compact.xml. Обновлены AUDIT.md и корневые/Android CHANGELOG.md/TEST_REPORT.md.

## Android 279 — оформление автооткрытия, 10.10.2026
Из карточки автооткрытия удалены видимый статус BLE-маяка, заголовок мощности, подписи по краям и текст текущего уровня. Остались название, переключатель и регулятор. Полоса регулятора плавно расширяется вправо от 4 до 14 dp; синяя часть заполняется до ползунка. Диапазон уровней 0–3, сохранение настройки, BLE-обработчик изменения мощности, касания и доступность SeekBar сохранены.
Изменены: MainActivity.kt, HorizontalRssiSlider.kt и Android BUILD_NUMBER; обновлены AUDIT.md, корневые и Android CHANGELOG.md/TEST_REPORT.md.

## Android 281 — виджет как иконка приложения, 10.10.2026
Виджет переведён на сетку 1×1, без фоновой карточки и дополнительных видимых надписей. Единственная подпись — TheKidLock. Размер изображения взят из системного @android:dimen/app_icon_size. При выключенном режиме используется существующая иконка ic_launcher; при включённом — её вариант ic_launcher_open с открытой дужкой и теми же корпусом/цветами. Фоновое переключение режима сохранено, описание состояния оставлено только для доступности. Старый экземпляр может сохранять прежний размер ячеек лаунчера; для сетки 1×1 нужно удалить его и добавить заново.
Изменены: AutoOpenWidget.kt, res/layout/widget_auto_open.xml, res/xml/widget_auto_open_info.xml, Android BUILD_NUMBER; добавлен res/drawable/ic_launcher_open.xml. Обновлены AUDIT.md и корневые/Android CHANGELOG.md/TEST_REPORT.md.

## ESP 10.0.453 — uptime в настройках замка, 10.10.2026
В таблицу диагностики панели CFG добавлена строка «Время работы (uptime)» в формате дней, часов, минут и секунд. Данные берутся из существующего uptime_s API /api/status и обновляются существующим refreshFast. При ошибке чтения отображается «—». API, таймеры и алгоритмы замка не менялись.
Изменены: main/web/index.html, BUILD_NUMBER, AUDIT.md, CHANGELOG.md, TEST_REPORT.md. Прошивка 453 опубликована на GitHub для самостоятельной установки с телефона; предыдущий релиз/тег 451 удалён штатным механизмом хранения только последнего релиза.

## Android 283 — виджет без подписи с меткой ярлыка, 10.10.2026
Подпись TheKidLock под виджетом удалена. В нижнем левом углу иконки добавлена маленькая стрелка на светлой подложке, как метка ярлыка Windows. Системный размер иконки, сетка 1×1, открытый/закрытый замок и фоновое переключение сохранены.
Изменены: res/layout/widget_auto_open.xml, res/xml/widget_auto_open_info.xml, Android BUILD_NUMBER; добавлен res/drawable/ic_widget_shortcut_badge.xml. Обновлены AUDIT.md, корневые и Android CHANGELOG.md/TEST_REPORT.md.

## Android 285 — размер виджета как у значков MIUI, 10.10.2026
Изображение виджета увеличено с системных 48 dp до 60 dp, что соответствует обычным значкам на текущем телефоне. Размер выделен в widget_icon_size и использован в макете и минимальных размерах провайдера. Подписи нет, метка ярлыка и смена замка по состоянию сохранены.
Изменены: res/layout/widget_auto_open.xml, res/xml/widget_auto_open_info.xml, Android BUILD_NUMBER; добавлен res/values/dimens.xml. Обновлены AUDIT.md, корневые и Android CHANGELOG.md/TEST_REPORT.md.


## Android 288 — обновление приложения через GitHub, 10.10.2026
Добавлен независимый от OTA замка downloader APK: проверка при запуске и примерно каждые 6 часов через WorkManager, ручная проверка внизу главного экрана. APK хранится в приватном каталоге; размер, SHA-256, пакет, версия и совпадение подписанта проверяются до сохранения и повторно перед установкой. Уведомление и кнопка открывают описание выпуска и штатный установщик Android; при необходимости предлагается разрешение установки из TheKidLock. После обновления кеш очищается.
versionCode теперь равен BUILD_NUMBER; создан постоянный release-ключ вне Git. build-android.ps1 получил Action release; publish-android.ps1 публикует APK/JSON и сохраняет один app-релиз, не затрагивая ESP. ESP publisher очищает только esp-v релизы и теги. Первый переход с debug требует переустановки, потеря настроек разрешена пользователем.
Изменены: build-android.ps1, Android BUILD_NUMBER, app/build.gradle.kts, proguard-rules.pro, AndroidManifest.xml, MainActivity.kt, BootReceiver.kt; добавлены AppUpdateClient.kt, AppUpdatePolicy.kt, AppUpdateWorker.kt, res/xml/app_update_paths.xml, AppUpdatePolicyTest.kt, publish-android.ps1, tools/prepare_android_signing.py, tools/publish_android_release.py, tools/test-app-release-retention.py; обновлены tools/publish_esp32_release.py и tools/test-release-retention.py. Документация: README.md, AUDIT.md, корневые и Android CHANGELOG.md/TEST_REPORT.md.


## Android release 289 — постоянная подпись, 10.10.2026
Первая оптимизированная release-сборка 1.0.289 успешно собрана проектным скриптом. APK 435452 байт подписан постоянным ключом; сертификат SHA-256 be9a249131a80bfe499c35dbad0752a13be5c4d02e4198b9da436e30d59885c0. Локальный APK сохранён в production/android-release-289 для первоначальной установки и проверки последующего обновления.


## Android 290 — выпуск и проверка обновления с GitHub, 10.10.2026
Ручная проверка заменяет ожидающую проверку при запуске, чтобы запрос пользователя не пропускался из-за кеша. В приватном каталоге сохраняется только последний проверенный APK; соединение освобождается и при ошибке установления HTTP-связи. Первая release 289 установлена вместо debug с разрешённой потерей настроек; постоянная подпись сохраняется для следующих обновлений. Новый выпуск 1.0.290 опубликован; app-v1.0.289 удалён, ESP 10.0.453 сохранён как global latest.
Изменены на этом этапе: AppUpdateClient.kt, AppUpdateWorker.kt, Android BUILD_NUMBER, корневые и Android CHANGELOG.md/TEST_REPORT.md. Публикация: production/android-release-290.


## Android 292 — исправление интернет-открытия в release, 10.10.2026
Устранена регрессия первого перехода на оптимизированный release APK: R8 удалял конструктор Paho JSR47Logger, создаваемый библиотекой через reflection, и MQTT-клиент не мог инициализироваться. Добавлено узкое keep-правило только для JSR47Logger. Защита владельца, InternetAccess, HMAC и алгоритмы замка сохранены. Добавлен tools/check_android_mqtt_release.py: проверяет фактическую фабрику MQTT-логгера установленного minified APK через временную подписанную instrumentation, без сети и команды замку; тестовый пакет удаляется, ориентация восстанавливается.
Изменены: android_lock_beacon/app/proguard-rules.pro, android_lock_beacon/BUILD_NUMBER, AUDIT.md, корневые и Android CHANGELOG.md/TEST_REPORT.md. Добавлен tools/check_android_mqtt_release.py. Подписанная release 292 установлена поверх 290 по Wi-Fi 192.168.1.65:33719 и опубликована; прежний app-релиз 290 удалён, ESP 453 сохранён.


## ESP 10.0.455 — Web UI по обновлённому index_updated.html, 10.10.2026
Перед изменениями созданы коммиты af3eef0 (прошивка 453) и 84aa155 (приложение 292). Uptime перенесён в верхний блок над версией. Домен брокера и кнопка сохранения размещены в CFG после диагностики. Отдельная MQTT-панель и переключатель интернет-открытия удалены; saveMqtt сохраняет enabled=true через прежний API. Backend, NVS и алгоритмы не менялись; само открытие страницы не включает MQTT и не пишет настройки.
Изменены: main/web/index.html, BUILD_NUMBER, AUDIT.md, CHANGELOG.md, TEST_REPORT.md. index_updated.html пользователя сохранён без изменений; целевой HTML совпадает с ним побайтно. Android после checkpoint не менялся.

Прошивка 455 опубликована для установки через приложение; предыдущая ESP 453 удалена после проверки загрузки, APP 292 сохранён.


## Android 294 — активная строка обновления, 10.10.2026
Убрана кнопка добавления виджета на главном экране и её pin-helper; существующий виджет сохранён. При каждом открытии экрана выполняется принудительная проверка GitHub. Ручной проверки и её текста больше нет. Только для скачанного/проверенного APK видна активная строка «Есть обновление», нажатие напрямую вызывает установщик без диалога приложения.
Изменены: MainActivity.kt, Android BUILD_NUMBER, README.md, AUDIT.md, корневые и Android CHANGELOG.md/TEST_REPORT.md.

Android 295 опубликован для проверки строки: при открытии экрана на 294 автоматически скачан новый APK и показано «Есть обновление». Следующий выпуск устраняет найденный системный chooser APK/WPS. Изменение сборки: Android BUILD_NUMBER; отчёты обновлены.


## Android 296 — прямой системный установщик, 10.10.2026
При установке проверенного APK существующий ACTION_VIEW Intent направляется явно в системный обработчик APK, найденный PackageManager. Убрано окно выбора между системным установщиком и сторонними приложениями; FileProvider/ClipData/read grant и подтверждение Android сохранены. Изменены MainActivity.kt, Android BUILD_NUMBER и отчёты.


## Android 297 — активная строка обновления, 10.10.2026
Удалена кнопка добавления виджета с главного экрана. При каждом открытии выполняется проверка GitHub; ручная строка проверки отсутствует. После загрузки и проверки новой версии появляется только активная строка «Есть обновление», которая сразу вызывает системный установщик APK. Системный обработчик выбирается явно, без выбора между установщиком и WPS. Изменены MainActivity.kt, Android BUILD_NUMBER, README.md и отчёты; существующий виджет сохранён.


## Android 299 — компактный виджет, 10.10.2026
Значок автооткрытия уменьшен с 60 до 48 dp, метка ярлыка с 16 до 12 dp. Минимальная область провайдера отделена от размера значка и уменьшена до 40 dp; целевая ячейка 1×1 сохранена. Ограничено рисование дочерних элементов областью макета. Изменены res/values/dimens.xml, res/xml/widget_auto_open_info.xml, res/layout/widget_auto_open.xml и BUILD_NUMBER; обновлены AUDIT/CHANGELOG/TEST_REPORT. Логика переключения и графика замка сохранены.


## Android 301 — область виджета 1×1, 10.10.2026
Исправлены ограничения provider: minWidth/minHeight и minResizeWidth/minResizeHeight 1 dp, maxResizeWidth/maxResizeHeight 60 dp, разрешено изменение размера по обеим осям, targetCell 1×1 сохранён. Значок возвращён к 60 dp — размеру обычного значка на телефоне. Существующий виджет на рабочем столе пользователя сжат штатными ручками launcher до одной ячейки. Изменены res/values/dimens.xml, res/xml/widget_auto_open_info.xml, BUILD_NUMBER и отчёты. Переключение режима не менялось.


## ESP 457 — единая кнопка обновления, 10.10.2026
В main/web/index.html кнопка сразу называется «Установить обновление». Удалены выбор .bin, имя выбранного файла, их CSS и useInternalFirmware, переименовывавшая кнопку. uploadFw использует только прошивку из /__app/firmware в приложении; вне приложения показывает подсказку открыть TheKidLock. OTA, сохранение номера передачи и подтверждение после перезапуска сохранены. Изменены main/web/index.html, BUILD_NUMBER, AUDIT.md, CHANGELOG.md и TEST_REPORT.md. Android-код не менялся; условный вызов старой функции остаётся для совместимости телефона с прежними прошивками и не выполняется на новой странице.


## ESP 459 — мастер настройки в два этапа, 10.10.2026
В main/web/calibration.html оставлены только закрытая и открытая дверь. Удалены упоминания бумаги из описания, инструкции, озвучки и результата, третий пункт «Готово» и дублируемый frontend-расчёт бумажных точек. Сохранены автоматические измерения, контроль притяжения в первом этапе, ограничения диапазона и завершение после озвучки. Backend/ULP/NVS не изменены. Изменены main/web/calibration.html, BUILD_NUMBER и AUDIT/CHANGELOG/TEST_REPORT.


## ESP 461 — расположение мастера, 10.10.2026
Кнопка «Мастер настройки замка» перенесена из блока телефонов первой строкой в панель «Настройки замка», сразу под заголовком и перед «Зоной открытия». Обработчик /calibration и мастер не изменены. Изменены main/web/index.html, BUILD_NUMBER и AUDIT/CHANGELOG/TEST_REPORT.


## Android 304 — проверка прошивки при открытии, 10.10.2026
MainActivity.onResume запускает проверку ESP-прошивки сразу, без прежней задержки 10 секунд. Если запрос уже выполняется, второй не запускается; завершение назначает один следующий фоновый запрос. Скачивание и индикатор доступности сохранены. Изменены MainActivity.kt, Android BUILD_NUMBER, README.md и отчёты.


## ESP 463 — название мастера калибровки, 10.10.2026
«Мастер настройки замка» переименован в «Мастер калибровки замка»: кнопка main/web/index.html, title и h1 main/web/calibration.html. Изменены эти два HTML, BUILD_NUMBER и AUDIT/CHANGELOG/TEST_REPORT. Поведение не менялось.


## ESP 466 — длительность сигнала открытого замка, 10.10.2026
Добавлен ползунок 10–200 мс, шаг 10 мс, значение по умолчанию 20 мс. Значение отображается при движении; после отпускания сохраняется в NVS blelock/departure_ms. При ошибке сохранения интерфейс возвращает последнее сохранённое значение. Изменён только короткий сигнал открытого замка; пауза 200 мс и остальные сигналы сохранены.
Изменённые файлы этапа: main/app_config.h, main/app_config.c, main/main.c, main/web_ui.c, main/web/index.html, BUILD_NUMBER, tools/test-app-config-defaults.py, tools/test-departure-pulse.py, AUDIT.md, CHANGELOG.md, TEST_REPORT.md. Android не изменялся.

Публикация esp-v10.0.466 успешна; SHA-256 публичных latest и tagged загрузок совпадает. Предыдущий ESP 463 удалён, APP 304 сохранён. Комплект production/esp-10.0.466/, подтверждение production/verification/departure-pulse-466/public-release.json.


## ESP 468 — постоянный ритм бипера, 10.10.2026
По уточнению пользователя диапазон длительности импульса 5–160 мс, шаг 1 мс. Значение в мс показывается при движении ползунка. Вместо постоянной паузы используется фиксированный прежний расчётный период 18 ticks / 180 мс; ширина импульса задаётся одноразовым esp_timer независимо от системного шага 10 мс. Остальные звуковые шаблоны, таймеры А/Б и алгоритм открытия сохранены.
Изменены main/main.c, main/app_config.h, main/web/index.html, tools/test-departure-pulse.py, BUILD_NUMBER, AUDIT.md, CHANGELOG.md, TEST_REPORT.md. Android не изменён.

ESP 468 опубликован; SHA-256 публичных latest и tagged загрузок проверен. Предыдущий ESP 466 удалён, APP 304 сохранён. Установку пользователь выполняет самостоятельно.


## ESP 471 — тест звука ползунком, 10.10.2026
Движение ползунка запускает прослушивание выбранного импульса на пять секунд; каждый следующий принятый запрос заново отсчитывает пять секунд. Тест независимо от состояния двери и departure_beep использует тот же ритм и генератор departure_buzzer_tone. По завершении возобновляется штатная логика звука. Аварийные/ручные сигналы сохраняют приоритет. Длительность 5–160 мс с шагом 1 мс и показ мс сохранены.
Новый phone-only POST /api/config/departure-buzzer-preview меняет только RAM; запись выбранного значения NVS остаётся при отпускании. Пока один запрос идёт, Web UI объединяет промежуточные изменения в последнее значение. Загрузка страницы и откат значения не запускают звук.
Изменённые файлы: main/main.c, main/app_diagnostics.h, main/web_ui.c, main/web/index.html, tools/test-departure-pulse.py, BUILD_NUMBER, AUDIT.md, CHANGELOG.md, TEST_REPORT.md. Android не изменялся.

ESP 471 опубликован; публичные latest/tagged SHA-256 совпадают с итоговой сборкой. ESP 468 удалён, APP 304 сохранён. Комплект production/esp-10.0.471/, подтверждение production/verification/departure-preview-471/public-release.json.


## ESP 473 — ползунок задержки закрытия, 10.10.2026
Числовое поле задержки после выхода из зоны заменено полноширинным ползунком 3000–10000 мс с отображением текущего значения при движении. Шаг 1 мс сохраняет произвольные прежние значения внутри диапазона. Настройка сохраняется существующей кнопкой «Сохранить». API/NVS, алгоритм таймера А и Android не изменены.
Изменённые файлы этапа: main/web/index.html, BUILD_NUMBER, AUDIT.md, CHANGELOG.md, TEST_REPORT.md.

ESP 473 опубликован, SHA-256 публичных latest и tagged загрузок совпадает с итоговой сборкой. ESP 471 удалён, APP 304 сохранён. Комплект production/esp-10.0.473/, подтверждение verification/delay-slider-473/public-release.json.


## ESP 475 — шаг задержки 100 мс, 10.10.2026
Ползунок задержки закрытия: 3000–10000 мс, шаг 100 мс. Отображение в секундах до десятых: 3,0–10,0 с. API и NVS остаются в миллисекундах; сохранение прежней кнопкой. Изменены main/web/index.html, BUILD_NUMBER, AUDIT.md, CHANGELOG.md, TEST_REPORT.md. Android и алгоритм таймера А не менялись.

ESP 475 опубликован. Первоначальный publisher завершился с ненулевым кодом после загрузки; отдельная проверка подтвердила публичные latest/tagged SHA-256, затем удалён ESP 473. APP 304 сохранён. Комплект production/esp-10.0.475/, подтверждение verification/delay-slider-475/public-release.json.


## ESP 478 — проверенный список MQTT, 10.10.2026
Пользовательский adress_broker.txt перенесён из корня в main/web/adress_broker.txt. Перед каждой сборкой CMake проверяет CONNACK без авторизации (MQTT 3.1.1, порт MQTT_PORT=1883) через Paho 2.1.0. Повторы удаляются в обработке, исходный файл сохранён. Только принятые адреса компилируются в прошивку, отчёт находится в build-<профиль>/generated/mqtt_brokers_report.json. Если никто не прошёл, сборка остановится. UTF-8/BOM и UTF-16/BOM поддерживаются. Зависимость устанавливается проектным build-esp32.ps1 в .eim-codex/python-deps.
Web UI: прежнее текстовое поле брокера с datalist; список GET /api/mqtt broker_choices, выбор или ручной ввод, сохранение прежней кнопкой. Список не записывается NVS, сохраняется только host. Подписки/публикации проверка не делает. Все 10 уникальных адресов этой сборки приняты.
Изменены CMakeLists.txt, main/CMakeLists.txt, build-esp32.ps1, main/web_ui.c, main/web/index.html, BUILD_NUMBER, main/web/adress_broker.txt (перенос), tools/check_mqtt_brokers.py, tools/test_mqtt_brokers.py, tools/requirements-build.txt, README.md, AUDIT.md, CHANGELOG.md, TEST_REPORT.md. Android и MQTT-команды не изменены.

ESP 478 опубликован; публичные latest/tagged SHA-256 совпадают с итоговой сборкой. ESP 475 удалён, APP 304 сохранён. Комплект production/esp-10.0.478/, подтверждение verification/mqtt-choices-478/public-release.json.


## ESP 480 — раскрытие списка брокеров, 10.10.2026
Datalist заменён явной стрелкой справа в поле и HTML-списком всех адресов. Раскрытие не зависит от браузерных подсказок и текущего текста. Выбор меняет адрес в поле без немедленной записи; сохраняет прежняя кнопка. Ручной ввод сохранён. Список закрывается после выбора/ввода, повторной стрелки, Escape и клика вне поля. При пустом списке ручной ввод доступен. Изменены main/web/index.html, BUILD_NUMBER, AUDIT.md, CHANGELOG.md, TEST_REPORT.md. Android, API/NVS и проверка брокеров не менялись.

ESP 480 опубликован; публичные latest/tagged SHA-256 совпадают с итоговой сборкой. ESP 478 удалён, APP 304 сохранён. Проверки: verification/mqtt-picker-480/, git diff --check успешен.


## Android 306 — адрес брокера в кнопке, 10.10.2026
В InternetOpenProgressButton пояснение send_remote_command заменено на host из InternetAccess.load(): источник совпадает с Интернет-командой InternetOpen.start. Изменения sealed обновляют подпись через SharedPreferences listener; подписка при attach и снятие при detach. Пустое значение — «—», длинное сокращается до одной строки с полным адресом в tooltip/contentDescription. Прогресс стадий и настройки подключения не менялись.
Изменённые файлы этапа: android_lock_beacon/app/src/main/java/com/blelock/app/InternetAccess.kt, android_lock_beacon/app/src/main/java/com/blelock/app/InternetOpenProgressButton.kt, android_lock_beacon/BUILD_NUMBER, AUDIT.md, CHANGELOG.md, TEST_REPORT.md, android_lock_beacon/CHANGELOG.md, android_lock_beacon/TEST_REPORT.md.

APP 306 опубликован: SHA-256 eea3422a5f020c48c63d43bfecd1b5ba23a4f7402183530dd27b2ef67221a34c. Публичные APK/metadata проверены publisher, APP 304 удалён, ESP global latest сохранён. Комплект production/android-release-306/.
