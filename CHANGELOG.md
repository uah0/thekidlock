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
