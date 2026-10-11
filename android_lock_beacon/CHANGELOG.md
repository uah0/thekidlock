# Текущий Android-проект

Исходники приложения сохранены без изменений при очистке 10.10.2026. Текущий локальный debug APK и metadata перенесены в ../production/android-debug/. Кэши и старые APK разработки убраны в Корзину. Для сборки использовать корневой build-android.ps1. Signing, applicationId и release настройки не менялись.

## Индикатор обновления — build 255
Номер downloaded GitHub release хранится с encrypted файлом. При начале OTA POST номер отмечается для текущего замка; ошибки передачи его не отменяют, подтверждение ESP не требуется. Если номера отличаются, в кнопке Настройки появляется меньшая строка «доступно обновление». Старый cache без номера загружается заново. Режим PhoneBeacon поддержан через существующий ble_peer. Итоговый APK 255 установлен на физический телефон, ориентация сохранена.

## Названия главных кнопок — Android build 257
«Автоматический режим» заменён на «Автооткрытие замка», «Замок постоянно открыт» — на «Оставить замок открытым», «Настройки» — на «Настройки замка». Подпись теперь «(доступно обновление)», чёрная через ForegroundColorSpan; меньший размер 0.6 сохранён. Изменены app/src/main/res/values/strings.xml и MainActivity.kt, Android BUILD_NUMBER и отчёты. Логика замка/OTA не менялась. Итоговый APK установлен на физический телефон.

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


## Android 294 — активная строка обновления, 10.10.2026
Убрана кнопка добавления виджета на главном экране и её pin-helper; существующий виджет сохранён. При каждом открытии экрана выполняется принудительная проверка GitHub. Ручной проверки и её текста больше нет. Только для скачанного/проверенного APK видна активная строка «Есть обновление», нажатие напрямую вызывает установщик без диалога приложения.
Изменены: MainActivity.kt, Android BUILD_NUMBER, README.md, AUDIT.md, корневые и Android CHANGELOG.md/TEST_REPORT.md.

Android 295: проверены автоматическое обнаружение релиза и активная строка «Есть обновление»; найден chooser Android/WPS при ACTION_VIEW, устраняется следующим этапом.


## Android 296 — прямой системный установщик, 10.10.2026
При установке проверенного APK существующий ACTION_VIEW Intent направляется явно в системный обработчик APK, найденный PackageManager. Убрано окно выбора между системным установщиком и сторонними приложениями; FileProvider/ClipData/read grant и подтверждение Android сохранены. Изменены MainActivity.kt, Android BUILD_NUMBER и отчёты.


## Android 297 — активная строка обновления, 10.10.2026
Удалена кнопка добавления виджета с главного экрана. При каждом открытии выполняется проверка GitHub; ручная строка проверки отсутствует. После загрузки и проверки новой версии появляется только активная строка «Есть обновление», которая сразу вызывает системный установщик APK. Системный обработчик выбирается явно, без выбора между установщиком и WPS. Изменены MainActivity.kt, Android BUILD_NUMBER, README.md и отчёты; существующий виджет сохранён.


## Android 299 — компактный виджет, 10.10.2026
Значок автооткрытия уменьшен с 60 до 48 dp, метка ярлыка с 16 до 12 dp. Минимальная область провайдера отделена от размера значка и уменьшена до 40 dp; целевая ячейка 1×1 сохранена. Ограничено рисование дочерних элементов областью макета. Изменены res/values/dimens.xml, res/xml/widget_auto_open_info.xml, res/layout/widget_auto_open.xml и BUILD_NUMBER; обновлены AUDIT/CHANGELOG/TEST_REPORT. Логика переключения и графика замка сохранены.


## Android 301 — область виджета 1×1, 10.10.2026
Исправлены ограничения provider: minWidth/minHeight и minResizeWidth/minResizeHeight 1 dp, maxResizeWidth/maxResizeHeight 60 dp, разрешено изменение размера по обеим осям, targetCell 1×1 сохранён. Значок возвращён к 60 dp — размеру обычного значка на телефоне. Существующий виджет на рабочем столе пользователя сжат штатными ручками launcher до одной ячейки. Изменены res/values/dimens.xml, res/xml/widget_auto_open_info.xml, BUILD_NUMBER и отчёты. Переключение режима не менялось.


## Android 304 — проверка прошивки при открытии, 10.10.2026
MainActivity.onResume запускает проверку ESP-прошивки сразу, без прежней задержки 10 секунд. Если запрос уже выполняется, второй не запускается; завершение назначает один следующий фоновый запрос. Скачивание и индикатор доступности сохранены. Изменены MainActivity.kt, Android BUILD_NUMBER, README.md и отчёты.


## Android 306 — адрес брокера в кнопке, 10.10.2026
В InternetOpenProgressButton пояснение send_remote_command заменено на host из InternetAccess.load(): источник совпадает с Интернет-командой InternetOpen.start. Изменения sealed обновляют подпись через SharedPreferences listener; подписка при attach и снятие при detach. Пустое значение — «—», длинное сокращается до одной строки с полным адресом в tooltip/contentDescription. Прогресс стадий и настройки подключения не менялись.
Изменённые файлы этапа: android_lock_beacon/app/src/main/java/com/blelock/app/InternetAccess.kt, android_lock_beacon/app/src/main/java/com/blelock/app/InternetOpenProgressButton.kt, android_lock_beacon/BUILD_NUMBER, AUDIT.md, CHANGELOG.md, TEST_REPORT.md, android_lock_beacon/CHANGELOG.md, android_lock_beacon/TEST_REPORT.md.

APP 306 опубликован: SHA-256 eea3422a5f020c48c63d43bfecd1b5ba23a4f7402183530dd27b2ef67221a34c. Публичные APK/metadata проверены publisher, APP 304 удалён, ESP global latest сохранён. Комплект production/android-release-306/.
## 1.0.308 — усиление защиты, 10.10.2026

Проверка каждого OTA redirect до подключения, HTTPS allowlist и предел шесть запросов;
loopback HTTP только debug. Явные запреты cloud/D2D backup для всех CE/DE доменов.
Очистка plaintext и одиночного BLE MQTT буфера в finally. Добавлены шесть регрессионных
сценариев; release сборка и 68 debug + 68 release unit-тестов успешны.
Изменены BUILD_NUMBER, CHANGELOG.md, TEST_REPORT.md, app/src/main/AndroidManifest.xml,
app/src/main/java/com/blelock/app/FirmwareUpdateClient.kt, InternetAccess.kt, PhoneBeacon.kt,
app/src/main/res/xml/backup_rules.xml, data_extraction_rules.xml,
app/src/test/java/com/blelock/app/FirmwareUpdateClientTest.kt и корневые отчёты аудита.
Подпись и applicationId сохранены; APK подготовлен локально, установка/публикация не выполнялись.
## 1.0.310 — языки en/ru/uk, этап 1
English fallback, русский/украинский resources и системный localeConfig. Динамические
UI строки, ошибки, уведомления и accessibility переведены через getString; команды неизменны.
Сборка и 71+71 unit-тестов успешны; список файлов в корневом CHANGELOG.md.
## 1.0.311 — языки en/ru/uk, итог
Добавлен явный English source set; foreground notification и widget обновляют язык
при configuration change, не переключая BLE-режим. English fallback, русский и украинский
ресурсы полные, системный localeConfig en/ru/uk проверен в APK. Web UI ESP остаётся русским.
Изменены values-en/strings.xml, BleLockService.kt, MainActivity.kt,
LocalizationResourcesTest.kt, BUILD_NUMBER и отчёты. Release и 71+71 тестов успешны.
Установка не состоялась из-за ошибки ADB shell 33719; APK подготовлен в production/android-release-311/.
## 1.0.312 — многоязычность завершена
English/українська добавлены, русский сохранён. Отдельное уведомление LOCKED сообщает
о замке без утверждения о двери. Наборы ресурсов 177 элементов, 71+71 тестов успешны.
Последний этап: BleLockService.kt, четыре strings.xml, BUILD_NUMBER и отчёты.
APK установлен по Wi-Fi; app locale [] и ориентация 0/0 восстановлены.
Web UI остаётся русским, публикация не выполнялась. APK production/android-release-312/thekidlock.apk.

## Android 316 — экран параметров и язык

Добавлен экран меню из шестерёнки: переход к прежней системной странице приложения, вход-заглушка будущего симулятора замка и список доступных языков. Выбор English / Русский / Українська сохраняется; Android 13+ использует application locale, старые поддерживаемые версии локализованный Context. Изменены MainActivity.kt, BleLockService.kt, добавлен AppLanguage.kt и строки ресурсов четырех наборов. Сборка и ограничения указаны в TEST_REPORT.md.
Финальный release build 316 успешен. Unit tests не запускались; проверка на телефоне отложена.
APK 1.0.316 установлен на телефон по ADB Wi-Fi после reconnect. Проверена версия и сохранены настройки ориентации 0/0; UI визуально не проверен.

## Android 318 — значки кнопки настроек замка

У кнопки Web UI замка шестерёнка слева и стрелка справа во всех состояниях текста.
Сборка 1.0.318 успешно установлена на телефон; настройки ориентации 0/0 сохранены и восстановлены.

## Android — JSON-переводы Web UI, подготовка

Планируемые словари Web UI en/ru/uk в assets APK и внедрение в ESP-страницы через LockWebUiDialog; подробности и итоговая сборка будут добавлены после реализации.

## Android 320 — JSON-переводы Web UI

Добавлены переводы основной страницы и мастера калибровки на English, русский и українська в assets JSON. WebView применяет выбранный язык приложения до показа страницы; калибровочная речь и ошибки локализованы тем же словарём. Release 1.0.320 собран и установлен на телефон.

## Android 323 — симулятор замка

Кнопка подключения симулятора добавлена в меню шестерёнки. Демо-экран имитирует
переключатели автооткрытия/удержания замка и пять этапов интернет-команды; реальные
BLE/MQTT команды не отправляются. Подписи доступны на английском, русском и украинском.
Debug и release unit tests по 71 тесту прошли; release 1.0.323 установлен по Wi-Fi ADB.

## Android 325 — виртуальный замок

В меню настроек добавлен переключатель режима симуляции. При включении главный экран
управляет виртуальным замком, а BLE/MQTT подключения к реальному замку блокируются.

## Android 327 — отмена симулятора, 11.10.2026

Код app/src/main восстановлен из b179041 (последний коммит до реализации симулятора).
Сохранены прежний интерфейс и локализация; прежний пункт-заглушка также восстановлен.
Номер сборки не понижен. Добавлен DEMO_VIDEO_PLAN.md: управление настоящим телефоном
через ADB, запись экрана, участие оператора двери и восстановление настроек.

Android 328: release-версия без реализации симулятора собрана и установлена на телефон
по Wi-Fi ADB с сохранением ориентации 0/0 и существующих данных приложения.

## Android 330 — меню настроек приложения, 11.10.2026

Убраны строка «Приложение» со стрелкой возврата и кнопка/заглушка симулятора.
Кнопка перехода к настройкам Android переименована в «Системные настройки приложения»
во всех локалях. Системный Back возвращает на главный экран; на Android 13+ применяется
OnBackInvokedCallback на время показа меню. Добавлены системные отступы меню.
Release 1.0.330 установлен на телефон; ADB-сценарий адаптирован к системному Back,
но запись ролика отложена по указанию пользователя.

## Android 332 — дефолт отсчёта Web UI, 11.10.2026

Начальное значение обратного отсчёта кнопки «Настройки замка» увеличено с 6 до 10 секунд
в LockSettings и MainActivity. Сохранённое измеренное время подключения используется
как прежде. Release 1.0.332 собран и установлен на телефон.
