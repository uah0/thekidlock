# Отчёт о проверках Android-приложения

## Web UI только через точку доступа телефона — baseline 04.10.2026

- Baseline APK build 182 собрана и установлена ранее; `build-android.ps1 -Action test` прошёл (`testDebugUnitTest`, `testReleaseUnitTest`).
- До и после установки Redmi Note 15 значения ориентации совпали: `accelerometer_rotation=1`, `user_rotation=0` (`artifacts/android-rotation-before-fresh-webui-ip-build182-install-20261004.json`).
- Для нового требования Android-код ещё не изменялся; runtime нового маршрута после обязательного создания hotspot предстоит проверить после реализации.

## Обновление IP Web UI при каждой кнопке «Настройки» — 04.10.2026

- Android `build-android.ps1 -Action debug`: PASS, build 182; `build-android.ps1 -Action test`: PASS (`testDebugUnitTest`, `testReleaseUnitTest`).
- APK установлена на Redmi Note 15. Системные `accelerometer_rotation=1`, `user_rotation=0` сохранены перед установкой, восстановлены и повторно проверены после неё (`artifacts/android-rotation-before-fresh-webui-ip-build182-install-20261004.json`).
- Физический сценарий при выключенном ESP автоподключении к домашнему Wi-Fi: PASS. Приложение не открыло старый `192.168.1.54`; после 5 секунд создало hotspot `172.18.51.2`, ESP прочитала Wi-Fi параметры через BLE, подключилась и передала IP `172.18.51.177` через encrypted BLE. Chrome показал Web UI версии 10.0.314 (`artifacts/webui-build182-runtime-20261004.log`, `artifacts/webui-build182-after-tap-20261004.png`).

## Получение Web UI IP от ESP — 04.10.2026

- Android build 182 и `build-android.ps1 -Action test`: PASS (debug/release unit tests). APK установлена; ориентация `1/0` восстановлена и проверена (`artifacts/android-rotation-before-fresh-webui-ip-build182-install-20261004.json`).
- ESP build 10.0.314: PASS; OTA `10.0.313 → 10.0.314` подтверждена device-agent.
- Runtime PASS: при пустом `lock_wifi_ip` bonded ESP отправила `192.168.1.54` зашифрованной BLE записью; Android сохранила его, Chrome автоматически открыл Web UI с версией 10.0.314.
- Ранее fallback создал LocalOnlyHotspot с IPv4 телефона `172.18.51.2`; полный сценарий подключения ESP без домашнего Wi-Fi дополнительно подтверждён физически в разделе выше.

## Android Web UI по кнопке «Настройки» — 04.10.2026

- Build 175 unit tests (`build-android.ps1 -Action test`): PASS.
- Redmi Note 15 runtime build 175: дефект воспроизведён; encrypted Wi-Fi chunks прочитаны ESP, hotspot истёк через 60 секунд, кнопка осталась в ожидании, browser intent не стартовал. `192.168.1.54` доступен в той же Wi-Fi сети и отвечает HTTP 200.
- Android build 176 `build-android.ps1`: PASS; APK установлена, ориентация экрана сохранена/восстановлена (`1/0`). Повторное нажатие и запуск браузера ожидают разблокировки телефона.
- `build-android.ps1 -Action test`: PASS, debug/release unit tests.
- Runtime build 176 на Redmi Note 15: PASS. Кнопка показала диалог адреса; после ввода `192.168.1.54` Chrome открыл Web UI замка, отобразились «Мастер настройки» и firmware 10.0.310. `lock_wifi_ip` сохранён.
- Повторное нажатие «Настройки»: PASS, Web UI сразу открылся без диалога.
- ESP после проверки: firmware 10.0.310, состояние `LOCKED`, `fault_flags=0`.

## Credentials GATT chunks — 04.10.2026

- Android baseline `assembleDebug`: PASS, build 171. Diagnostic build 173 установлен на Redmi Note 15; `accelerometer_rotation=0`, `user_rotation=0` сохранены и восстановлены.
- Runtime source buffer: PASS (`ssidBytes=17`, `passwordBytes=15`, `nulAt=-1`). ESP 10.0.309 видел NUL на ATT offset 22. Исправленный chunked протокол создан; сборка/установка APK и полная runtime-проверка ожидают выполнения.
- Chunked implementation Android build 174: PASS (`assembleDebug`). APK installation and device runtime validation pending.
- Build 174 installed on Redmi Note 15: PASS. `accelerometer_rotation=1`, `user_rotation=0` were saved before install, restored and verified after.
- Runtime build 174: PASS, Android/ESP прочитали два credentials блока `20 + 14` и `HOTSPOT_IP=172.18.51.2`; ESP 10.0.310 не переключилась на hotspot из-за сохранённой рабочей Wi-Fi priority policy. Найден сбой MainActivity: pending UI flag отсутствовал и сбрасывался на BLE disconnect. Исправленная state machine ещё требует сборки/установки/runtime проверки.
- MainActivity pending Web UI state fix: `build-android.ps1` PASS, Android build 175.
- Build 175 installed: PASS, Redmi Note 15 orientation `1/0` restored and verified. ESP Web UI at `192.168.1.54` responds HTTP 200. Physical button/browser validation awaits phone unlock; current device is at lock screen.

## Proof GATT read во время Wi-Fi provisioning — 04.10.2026

- Baseline `build-android.ps1` до изменения: PASS (`assembleDebug`, предыдущий Android build 170).
- После изменения `PhoneBeacon.kt`: `build-android.ps1` PASS (`assembleDebug`), Android build 171; предупреждения Kotlin прежние deprecation warnings.
- Установка APK на Redmi Note 15 через ADB: PASS. До/после установки `accelerometer_rotation=0`, `user_rotation=0`, значения совпали; снимок до установки: `artifacts/android-rotation-before-proof-provision-build171-install-20261004.json`.
- ESP доступна по сети: firmware 10.0.305, `LOCKED`, `fault_flags=0`.
- Runtime proof/Wi-Fi provisioning после изменения ещё не выполнен: экран телефона заперт. Ожидается пользовательская разблокировка, затем будет нажата «Настройки».

## Восстановление зависшего BLE autoConnect — 01.10.2026

- Исходный build 131 — PASS. На Redmi при близком ESP нативный запрос оставался в `CONNECTING` без колбэка более двух минут.
- Build 132 после правки — PASS (`build-android.ps1`, `assembleDebug`). APK установлен на Redmi Note 15; настройки ориентации 0/0 до установки и 0/0 после восстановления.
- Logcat: 14:42:42 native pending; 14:44:52 watchdog; 14:44:54 прямая попытка завершилась GATT 133; 14:44:59 native-запрос создан повторно. После перезапуска ESP в 14:47:06 GATT подключился, защищённое чтение `LOCK_STATUS` и запись `OPEN` успешны. ESP 10.0.248: `OPEN`, `connected=true`, `fault_flags=0`.
- Сервис подавил повторный OPEN при следующих чтениях RSSI. Причина недоступности BLE до перезапуска ESP не доказана; 90-секундный Handler-таймер сработал примерно через 130 секунд.

## Исправление BLE-обнаружения на Xiaomi — 28.09.2026

- `build-android.ps1` для build 112 — PASS; APK установлен на Xiaomi 2510DRA23E.
- До исправления сервис оставался в `SCANNING`, а logcat не содержал принятого
  BLE advertising callback. После исправления logcat: `BLE advertisement accepted`
  (RSSI телефона `-58`), `GATT_CONNECTED`; дальше прошли защищённое чтение и
  запись имени телефона.
- ESP32 status после исправления: `connected=true`, `encrypted=true`,
  `bonded=true`, RSSI `-41`, замок `LOCKED`, `fault_flags=0`.
- ESP event log подтвердил owner-protected чтение `ADMIN_STATE` на новой сессии.
- Ориентация до/после установки совпала (`accelerometer_rotation=0`,
  `user_rotation=0`). Текущую UI-иерархию после установки не удалось прочитать:
  телефон оставался заблокирован; до установки обе mode-кнопки наблюдались
  disabled в состоянии BLE-поиска.

## Owner-сессия BLE и один сигнал рекламы — 28.09.2026

- Baseline до изменений: `build-android.ps1` — PASS.
- Финальная Android-сборка build 111: `build-android.ps1` — PASS (`assembleDebug`).
- Поиск оставшихся вызовов звука: в приложении остался только
  `BleEventTones.advertisementDetected()` при принятом рекламном пакете замка.
- Тесты и установка на телефон в этой задаче не выполнялись; поведение после
  установки на устройстве ещё не проверено.

## Закрытие замка при отключении Always Open — 28.09.2026

- Android baseline `build-android.ps1` до изменения — PASS (`assembleDebug`).
- Baseline ESP build перед изменениями — PASS (build 225); по проектному
  правилу загружен на текущий замок. Возврат подтверждён, версия `10.0.225`,
  read-only статус `LOCKED`, `fault_flags=0`.
- Добавлен reducer test на команду выхода из Always Open. Итоговые Android
  `build-android.ps1` — PASS; `gradlew --no-daemon test` — PASS, debug/release;
  `WorkingModeReducerTest` 14/14.
- После уточнения same-button toggle итоговый APK пересобран как build 106,
  тесты повторены: 26 debug unit tests, 0 failures/errors (release task тоже
  прошёл). Build 106 установлен на Xiaomi; ориентация `0/0` восстановлена и
  проверена.
- Интерактивный прогон build 106: AUTO был selected; нажатие Always Open дало
  `OPEN_HOLD` и выделение Always Open. Повторное нажатие отправило
  `AUTO_OFF_CLOSE`; ESP подтвердил `LOCKED`, обе кнопки стали невыбранными.
  При повторном AUTO обнаружен старый native presence latch (`nativeOpenSent`),
  поэтому добавлена его очистка после подтверждённого закрытия.
- После этого build 107 собран и установлен; тесты повторены (26 debug tests,
  0 failures/errors; release task прошёл). Перед установкой настройки экрана
  сохранены, после установки восстановлены и проверены `0/0`.
- После восстановления Wi-Fi ADB интерактивный прогон build 107 завершён:
  AUTO отправил `OPEN`, ESP и ULP подтвердили `OPEN`; выделилась кнопка AUTO.
  Always Open подтвердился как `OPEN_HOLD` и выделилась только соответствующая
  кнопка. Повторное нажатие отправило `AUTO_OFF_CLOSE`, ESP подтвердил `LOCKED`,
  обе кнопки сняли выделение. Повторное включение AUTO после сброса presence
  latch снова открыло замок (`OPEN`); выключение AUTO отправило
  `AUTO_OFF_CLOSE`, ESP подтвердил `LOCKED`. Обе кнопки стали невыбраны; замок
  оставался `LOCKED`, `hold_open=false`, при RSSI телефона `-38 dBm`.

## Повторное автооткрытие при непрерывном присутствии — 28.09.2026

- До изменения baseline `build-android.ps1` завершился успешно (`assembleDebug`).
- `gradlew --no-daemon test` — PASS: debug и release unit tests; новый
  `AutoOpenPresenceGateTest` — 4/4, `WorkingModeReducerTest` — 13/13.
- `build-android.ps1` (`assembleDebug`) — PASS; APK build number 103 собран и
  установлен на Xiaomi 2510DRA23E через Wi-Fi ADB (`Success`). Пакет сообщает
  `versionName=1.0.0`, `versionCode=1`.
- До установки сохранены `accelerometer_rotation=0` и `user_rotation=0`;
  после установки оба значения восстановлены и прочитаны как `0/0`. Снимок
  сохранён в `.device-agent/auto-open-fix-orientation.json`.
- После обновления read-only статус ESP показал `OPEN`, без ошибок, encrypted и
  bonded BLE связь активна, RSSI `-41 dBm`. Физический повторный цикл
  в течение установки не наблюдался, но live logcat после установки зафиксировал
  `AUTO_OPEN_REARMED` (сервис получил `OPEN` и последующий `LOCKED`), а затем
  новый `AUTO_OPEN_RSSI_THRESHOLD_REACHED` при всё ещё близком телефоне. При
  коротком опросе ESP lock оставался `OPEN`; ошибок AndroidRuntime нет.

## Диагностика автооткрытия — 28.09.2026

- На телефоне GATT соединение с замком стало `READY`, encrypted status read
  прошёл. При `gattRssi=-59 dBm` и локальном пороге `-62 dBm` AUTO отправил
  `OPEN`; ESP приняла encrypted/bonded команду, ULP подтвердил `OPEN`, status
  ответил `open=true`, `locked=false`.
- Последующие RSSI `-52…-56 dBm` подавлялись с
  `nativeOpenSent=true`; повторного `OPEN` в том же присутствии сервис не
  отправляет. Позднее устройство получило `AUTO_OFF_CLOSE` и подтвердило
  `LOCKED`.
- `AUTO` на главном экране является toggle. При активном AUTO повторное
  нажатие выключает сервис и отправляет `AUTO_OFF_CLOSE`; для открытия по
  присутствию режим AUTO должен оставаться активным.
- Добавлен диагностический лог причины блокировки, ограниченный одним сообщением
  за 5 секунд. `gradlew test` — PASS; `build-android.ps1` (`assembleDebug`) —
  PASS. Сборка 101 установлена на Xiaomi 2510DRA23E через Wi-Fi ADB — `Success`.
- Перед установкой сохранены `accelerometer_rotation=0`, `user_rotation=0`;
  после установки восстановлены и проверены `0/0`. Снимок сохранён в
  `.device-agent/android-rssi-diagnosis-install-orientation.json`.
- На момент последней проверки замок отвечает `LOCKED`, соединение encrypted,
  RSSI выше порога; новая команда подавлена latch-ом присутствия. Ручной OPEN
  во время диагностики не отправлялся.

## Подтверждённые режимы главного экрана — 28.09.2026

- Baseline `build-android.ps1` до изменения — PASS (`assembleDebug`).
- `gradlew test` после изменения — PASS; debug и release unit test tasks
  выполнены успешно, включая переписанный `WorkingModeReducerTest`.
- Итоговый `build-android.ps1` — PASS (`assembleDebug`), debug APK создан.
- Добавлены проверки unknown/READY, AUTO ↔ Always Open с раздельным
  подтверждением замка и сервиса, выключения AUTO, отказа записи, timeout,
  disconnect, несовместимых out-of-order status, повторных событий и
  просроченного timeout.
- Debug APK установлен на Xiaomi 2510DRA23E через ADB Wi-Fi: `Success`;
  установленный пакет сообщает `versionName=1.0.0`, `versionCode=1`.
- Перед установкой сохранены `accelerometer_rotation=0` и `user_rotation=0`;
  после установки оба значения восстановлены и повторно прочитаны как `0/0`.
- Снимок исходной ориентации сохранён в
  `.device-agent/android-working-mode-install-orientation.json`.

## Удаление настройки «Долго открыт» — 28.09.2026

- `build-android.ps1`: PASS (`assembleDebug`). APK установлен на Xiaomi
  2510DRA23E. `accelerometer_rotation` и `user_rotation` до/после: `0/0`.
- Автоматизированные unit-тесты для удаления поля не запускались.

## Активный scan без таймаута пустого поиска — 28.09.2026

- Baseline `build-android.ps1` до изменения — PASS (`assembleDebug`).
- Итоговая `build-android.ps1` — PASS (`assembleDebug`), APK создан.
- APK установлен на Xiaomi 2510DRA23E: `Success`. До установки прочитаны
  `accelerometer_rotation=0`, `user_rotation=0`; после запуска приложения
  значения восстановлены и повторно проверены как `0/0`.
- На включённом экране приложение продолжало получать callback-результаты более
  15 секунд; ошибки `BLE Lock не найден за 15 секунд` в новом коде нет.
- После погашения экрана Xiaomi `BtGatt.ScanManager` приостановил app scan:
  в 14:54:09 зарегистрировано `suspend scan` для `com.blelock.app`,
  `scanModeApp=2`, `scanModeUsed=0`, `duration=8396 ms`. Приложение при этом
  оставалось foreground service. Это ограничение системного scanner manager,
  а не timeout из приложения.
- Полная проверка последующего возобновления callbacks после длительного Doze
  в этом прогоне не выполнялась.

## Настройка паузы восстановления BLE security — 27.09.2026

- Baseline `build-android.ps1` до изменений: PASS (`assembleDebug`).
- Добавлены сохранение настройки 0…2000 мс (default 700) и передача её в оба
  места `BleRepository`, где ранее использовалась константа 700 мс.
- Итоговый `build-android.ps1`: PASS (`assembleDebug`); APK установлен через
  Wi-Fi ADB на Xiaomi 2510DRA23E: `Success`.
- Перед установкой сохранены `accelerometer_rotation=0` и `user_rotation=0`;
  после установки оба значения восстановлены и проверены как `0/0`.
- Фактическое время открытия замка при изменении значения ползунка на устройстве
  отдельно не измерялось.

## Фоновое BLE-обнаружение замка — 26.09.2026

- Baseline до правок: `build-android.ps1` — PASS.
- `testDebugUnitTest` после изменений — PASS. Проверены auto-open OFF,
  отсутствующие permissions/Bluetooth, UUID и свежесть результата,
  подавление десяти дублирующих событий одним gate.
- Итоговый `build-android.ps1` — PASS; `assembleDebug` завершён успешно.
- После удаления таймерного LOW_LATENCY fallback выполнены повторные
  `testDebugUnitTest` и `build-android.ps1` — PASS.
- Проверка сканирования на Bluetooth hardware, доставки PendingIntent при
  process death, GATT/auth/open, departure и reboot на физическом телефоне не
  выполнялась в этом прогоне.
- Требуется отдельная проверка на телефоне API 31+ с выключенным экраном и
  завершённым процессом. На Android 12+ старт FGS из фона ограничен; Android
  указывает Companion Device Manager как путь для гарантированного фонового
  старта connected service. OEM autostart/battery controls могут дополнительно
  влиять на доставку.
- Сборка сохранила имеющиеся предупреждения о deprecated API/Kotlin options;
  ошибок компиляции или тестов нет.
- 26.09.2026 debug APK установлен через Wi-Fi ADB на Xiaomi 2510DRA23E:
  `Success`. `pm path` подтвердил пакет `com.blelock.app`; система сообщает
  versionCode=1, versionName=1.0.0, targetSdk=36.
- Перед установкой сохранены `accelerometer_rotation=0` и `user_rotation=0`;
  после установки оба значения восстановлены и повторно проверены как `0/0`.
  Снимок исходных значений: `.device-agent/android-ble-pi-install-orientation.json`.
- Функциональный проход с замком после установки не выполнялся.

## Однократное выключение AUTO — 25.09.2026

- Контрольная `build-android.ps1` до изменений: PASS.
- Итоговая `build-android.ps1`: PASS; 35 задач, 6 выполнено, 29 актуальны.
- APK установлен на телефон; ориентация сохранена и проверена: `1/0`.
- При выключении AUTO на реальном замке с `state=OPEN`, `plate=Далеко`
  приложение отправило одну `AUTO_OFF_CLOSE`; за 5 секунд повтора не было.
- После первого статуса замка все три основные кнопки доступны; замок
  корректно остаётся `OPEN`, faults=0.

## Стабильный вид главных кнопок — 25.09.2026

- Исходная `build-android.ps1` без изменений: PASS; 35 задач, 6 выполнено,
  29 актуальны.
- Проверка на телефоне подтвердила смену состояния `READY`/`OPEN_SENT` и
  мигание прозрачности главных кнопок во время `AUTO_OFF_CLOSE`.
- Итоговая `build-android.ps1`: PASS; 35 задач, 6 выполнено, 29 актуальны.
- Исправленный APK установлен на телефон, приложение перезапущено; значения
  ориентации `0/0` сохранены и проверены повторным чтением.

## Версия приложения над политикой конфиденциальности — 25.09.2026

- Контрольная `build-android.ps1` до изменений: PASS.
- `build-android.ps1`: PASS; 35 задач, 12 выполнено, 23 актуальны.
- Проверено: Android-версия не добавляется в URL; в Web UI отсутствует блок
  версии Android; firmware version остаётся связанной с `/api/status`.
- Нижний блок показывает `VERSION_NAME`, номер сборки и `BUILD_TIMESTAMP`.
- После резервирования высоты нижнего блока повторный `build-android.ps1`:
  PASS; 35 задач, 6 выполнено, 29 актуальны.
- Обновлённый APK установлен по Wi-Fi ADB; исходная ориентация `0/0`
  восстановлена и подтверждена повторным чтением.

## Настройки замка в секундах — 24.09.2026

- Контрольная debug-сборка до изменений: PASS.
- Статически проверено удаление полей GPIO/уровня из legacy и декларативной
  панели, чтения BLE-конфигурации и записи BLE-конфигурации.
- `build-android.ps1`: PASS; 35 задач, 12 выполнено, 23 актуальны.
- Существуют только прежние предупреждения deprecated API; ошибок Kotlin/Java
  и ресурсов нет.
- Установка APK не выполнялась: ADB-устройство `192.168.1.65:45119` сейчас не
  подключено. Настройки ориентации телефона не изменялись.

## Ожидание домашней Wi-Fi сети ESP32 — 23.09.2026

- Контрольная debug-сборка до изменений: PASS.
- Обработка `CONNECTING_WEB` возвращается к ожиданию адреса и не вызывает
  `startWebUiProvisioning()`.
- `build-android.ps1`: PASS; 35 задач, 6 выполнено, 29 актуальны.

## Автоматическое отключение точки доступа — 19.08.2026

- Добавлен сторож LocalOnlyHotspot с периодической проверкой IP через 15 секунд.
- Первый IP ESP32 запоминается; изменение IP приводит к закрытию точки доступа.
- Если ESP32 не сообщил IP за 15 секунд, точка доступа закрывается.
- Итоговая сборка APK после изменения: **BUILD SUCCESSFUL**.
- Ограничение Android API: LocalOnlyHotspot не предоставляет приложению
  публичный список подключённых клиентов; контроль выполняется по IP ESP32,
  переданному через `WEB_READY;IP=...` по BLE.

## Автоматическое подключение к Web UI — 19.08.2026

- Убрана отдельная кнопка ручного запуска точки доступа.
- Кнопка «Подключиться к Web UI» находится в общей панели телефона и постоянно
  присутствует независимо от видимости административного блока.
- Реализована проверка `WEB_READY;IP=...` по BLE перед запуском точки доступа.
- При отсутствии IP запускаются LocalOnlyHotspot Android и передача SSID/пароля
  по BLE без сохранения этих данных в ESP32.
- APK: `assembleDebug` — **BUILD SUCCESSFUL**.
- Аппаратная проверка сценария после установки APK ещё не выполнена.

## Восстановление отображения версии ESP32

- Изменён BLE-сервис: кэшированные данные передаются новому клиенту Activity.
- Базовая сборка APK до изменения: **BUILD SUCCESSFUL**.
- Итоговая сборка APK: **BUILD SUCCESSFUL**.

## Передача RSSI-контроля в ESP32

- Пороги RSSI удалены из Android-интерфейса и локальных настроек.
- Android пытается подключиться к обнаруженному BLE-маяку независимо от RSSI.
- Сборка APK: **BUILD SUCCESSFUL**.
- ESP32-часть нового контроля RSSI реализована отдельным этапом; её сборка
  зафиксирована в корневом отчёте `TEST_REPORT.md`.

## Удаление ADMIN_AUTH

- `assembleDebug`: **BUILD SUCCESSFUL**.
- APK: `app/build/outputs/apk/debug/app-debug.apk`, 860181 байт.
- Требуется проверить чтение и запись настроек после прошивки ESP32 без AUTH.

## Исправление запуска реактивного Pairing

- Причина: после service discovery Android переходил в `BONDING`, но не читал
  защищённую характеристику; обе стороны ожидали security-запрос.
- Добавлено чтение `LOCK_INFO` (`READ_ENC`) без явного `createBond()`.
- Требуется повторная сборка APK и аппаратная проверка одного системного
  Pairing-диалога.
- После исправления `assembleDebug`: **BUILD SUCCESSFUL**.
- Пользователь аппаратно подтвердил успешный системный Pairing после установки
  обновлённого APK.
- Статус исправления реактивного Pairing: **УСПЕШНО**.

## A5.2 — BLE-панель настроек

- `assembleDebug`: **BUILD SUCCESSFUL**.
- Kotlin-код AUTH, чтения, разбора шести полей и записи конфигурации успешно
  скомпилирован.
- APK: `app/build/outputs/apk/debug/app-debug.apk`, 860181 байт.
- Первоначальная ошибка `debug.keystore.lock` устранена переносом Android
  user-home в разрешённый локальный каталог; к исходному коду она не относилась.
- Требуется аппаратно проверить правильный/неправильный пароль, чтение,
  сохранение, переподключение и сохранность значений после reboot ESP32.

Статус A5.2: сборка завершена, аппаратная проверка ожидается.

## Изменение модели доступа

- Парольная характеристика удалена из GATT и Android-клиента.
- `ADMIN_CONFIG` требует только encrypted + bonded соединение.
- Требуется перепрошить ESP32 и повторить чтение/сохранение настроек.

## Исправление двойного Pairing

- Причина: параллельные `ble_gap_security_initiate()` на ESP32 и
  `BluetoothDevice.createBond()` на Android.
- Android-клиент пересобран без второго инициатора bonding.
- Требуется повторить первичную регистрацию с очищенным bond.

## Исправление окна регистрации

- При активном соединении фонового приложения ESP32 теперь отключает его при
  нажатии Web-кнопки регистрации.
- После disconnect advertising запускается повторно; новый телефон получает
  возможность Pairing/Bonding в 60-секундном окне.

## A5.1 — BLE admin protocol

- `ble_phone.c` успешно скомпилирован реальной Xtensa-командой из Ninja graph.
- Android `BleProtocol.kt` компилируется в составе debug APK.
- Требуется прошить ESP32 с новой GATT-моделью и выполнить ручную проверку AUTH
  и чтения конфигурации до включения панели в UI.
- Значение compile-time пароля намеренно не записывается в отчёт.

## Исправление BOOT_COMPLETED

- Причина воспроизведения: приложение начинало работать только после вывода
  Activity на экран, потому что отложенный запуск из receiver мог быть потерян.
- `BootReceiver` теперь запускает Foreground Service синхронно в `onReceive`.
- Требуется повторить reboot-тест без открытия Activity.
- Пользователь подтвердил: после перезагрузки Android 16 автоматическое BLE-
  открытие работает без вывода приложения на экран.

Статус A3: пройден.

## Исправление запуска после выдачи разрешений

- После завершения runtime permission dialog Foreground Service запускается
  автоматически, если автооткрытие уже включено.
- Кнопка разрешений при уже выданных разрешениях повторно запускает сервис.
- APK после исправления требует повторной проверки reboot-сценария.

## Исправление автозапуска

- Изменён BootReceiver: добавлена задержка 10 секунд и запуск после обновления
  пакета.
- Изменено сохранение настройки автооткрытия на синхронное `commit`.
- Требуется повторная проверка после перезагрузки телефона.

## A3 — фоновый Foreground Service

### Сборка

- `build-local.ps1` выполнен после добавления сервиса и BootReceiver.
- `assembleDebug`: `BUILD SUCCESSFUL`.
- APK обновлён: `app/build/outputs/apk/debug/app-debug.apk`.

### Требуемая аппаратная проверка

1. Установить новый APK поверх A2.
2. Выдать Bluetooth и notification permissions.
3. Включить «Автоматическое открытие».
4. Убедиться, что появилось постоянное уведомление `TheKidLock работает в фоне`.
5. Открыть окно регистрации, если телефон ещё не bonded.
6. Подойти к замку: сервис должен найти, подключиться и отправить OPEN без
   нажатия кнопки.
7. Заблокировать экран и повторить проверку с телефоном в кармане.
8. Уйти от замка, дождаться подтверждения ухода, затем снова приблизиться.
9. Перезагрузить телефон и проверить восстановление уведомления/сервиса.
10. Выключить переключатель и убедиться, что уведомление исчезает и BLE scan
    прекращается.

Статус аппаратной проверки A3: ожидается.

## A2 — ручное BLE-подключение и OPEN

### Сборка

- Kotlin-код BLE repository успешно скомпилирован.
- `assembleDebug` выполнен самостоятельно через `build-local.ps1` и JDK 17.
- Результат: `BUILD SUCCESSFUL`.
- APK: `app/build/outputs/apk/debug/app-debug.apk`.
- Предупреждение Android analytics связано с запретом записи вне workspace и не
  влияет на APK.

### Требуемая аппаратная проверка

1. Установить свежий debug APK.
2. Выдать Bluetooth и notification permissions.
3. Открыть в Web UI ESP32 60-секундное окно регистрации.
4. Нажать «Найти и зарегистрировать» и подтвердить системный Pairing.
5. Убедиться, что экран показывает LOCK_INFO и LOCK_STATUS.
6. Нажать «Открыть сейчас» при достаточном RSSI.
7. Проверить `BLE OPEN принят` в журнале ESP32 и физическое открытие.
8. Перезапустить приложение и убедиться, что повторного Pairing не требуется.

Статус аппаратной проверки A2: ожидается.

## A1 — каркас приложения

- Gradle Sync и Build APK успешно выполнены пользователем в Android Studio.
- Получен исходный `app-debug.apk` размером около 838 КБ.
- После появления локальных plugins самостоятельная offline-сборка агента стала
  возможна через общий read-only dependency cache.

Статус A1: завершён.

## A0 — аудит

- JDK 17.0.14 запускается по `C:\jdk17\bin`.
- Android SDK и platform/build-tools 35 установлены.
- ADB 37.0.0 запускается.
- Gradle 8.14 присутствует в wrapper cache.
- Android Gradle Plugin и Kotlin Gradle Plugin локально не обнаружены.
- Исходной Android-сборки до A0 не существовало, поэтому baseline APK отсутствует.
- GATT UUID, flags и формат OPEN сверены непосредственно с `main/ble_phone.c`.

Статус A0: завершён. Этап A1 ещё не начат.
# Исправление ожидания подключения ESP к точке доступа — 19.08.2026

## Проверки

- `:app:assembleDebug --no-daemon --console=plain` — успешно.
- APK создан: `app/build/outputs/apk/debug/app-debug.apk`.
- До получения первого IP точка доступа теперь не закрывается через 15 секунд.
- После получения IP контроль смены адреса выполняется каждые 15 секунд.

# Подготовка к Google Play — 19.08.2026

## Проверки

- `:app:assembleDebug --no-daemon --console=plain` — успешно.
- `:app:bundleRelease --no-daemon --console=plain` — успешно.
- Проверено наличие `app/build/outputs/bundle/release/app-release.aab`.
- Вариант release собирается без ключа, но AAB остаётся неподписанным до
  создания владельцем проекта `keystore.properties`.
- API 36 принят компилятором и обработан манифестом без ошибок.

## Не пройдено

- Проверка подписи release AAB — ожидает реальный ключ загрузки.
- Проверка публикационного объявления и политики конфиденциальности — ожидает
  публичный адрес политики и реквизиты разработчика.

# Политика конфиденциальности — 19.08.2026

## Проверки

- `:app:assembleDebug --no-daemon --console=plain` — успешно.
- `:app:bundleRelease --no-daemon --console=plain` — успешно.
- Политика доступна из главного экрана приложения.
- Исходный текст политики сохранён в `PRIVACY_POLICY.md`.

## Осталось

- Разместить текст политики на постоянном публичном HTTPS-адресе.
- Указать фактический контакт разработчика перед заполнением Play Console.

# Улучшение интерфейса — 19.08.2026

## Проверки

- `:app:assembleDebug --no-daemon --console=plain` — успешно.
- `:app:bundleRelease --no-daemon --console=plain` — успешно.
- Изменённые элементы: название сервисной кнопки, статус потери BLE, окно
  политики и отображение версии.

## Не проверено на экране устройства

- Визуальная проверка на телефоне не выполнена: подключённый телефон находится
  в режиме сна/на экране блокировки.

# Управление замком с главного экрана — 19.08.2026

## Проверки

- `:app:assembleDebug --no-daemon --console=plain` — успешно.
- `:app:bundleRelease --no-daemon --console=plain` — успешно.
- Команда `OPEN` переиспользует существующий BLE-протокол и единый BLE-владелец.
- Новая команда закрытия и изменения алгоритма ULP не добавлялись.

## Требует проверки на телефоне и ESP32

- Фактическое нажатие `Открыть` и `Не закрывать` на bonded-телефоне.
- Видимость `Настройки замка` после ответа ESP32 о правах администратора.
- Переход в Web UI из административной панели.

# Индикация ручного открытия — 19.08.2026

## Проверки

- `:app:assembleDebug --no-daemon --console=plain` — успешно.
- `:app:bundleRelease --no-daemon --console=plain` — успешно.
- Обе кнопки используют единый существующий BLE-вызов `OPEN`.
- Переключатель автоматического режима не участвует в обработчиках ручных
  кнопок.
- Цвет `Не закрывать` меняется только по статусу `OPEN`, полученному после
  команды.

## Не проверено на устройстве

- Нажатие кнопок на реальном телефоне и ESP32.
- Приём статуса `OPEN` от установленной на ESP32 прошивки.

# Исправление мигания кнопки — 19.08.2026

- Добавлено ожидание актуального статуса после `KEEP_OPEN`.
- Сборка `:app:assembleDebug` выполняется после изменения.
- Проверка на физическом телефоне пока не выполнялась.

# Режим «Не закрывать» — 19.08.2026

- Добавлена передача BLE-команды `KEEP_OPEN`.
- Добавлено отображение статуса `OPEN_HOLD`.
- Сборка `:app:assembleDebug --no-daemon --console=plain` — успешно.
- Установка на телефон и проверка на физическом ESP32 пока не выполнялись.

# Подключение для ручных кнопок — 19.08.2026

- Ручные кнопки больше не требуют предварительного включения автоматического
  режима.
- При выключенном автоматическом режиме нажатие кнопки запускает BLE-сеанс,
  после чего команда передаётся в состоянии `READY`.
- Требуется проверка на телефоне с выключенным автоматическим режимом.

# Упрощение панели настроек — 19.08.2026

- Панель начинается со строки `Сеть:` без дополнительных надписей сверху.
- Политика конфиденциальности скрывается на время открытия панели.
- `assembleDebug` — успешно.

# Устранение повторной отправки KEEP_OPEN — 19.08.2026

- Исправлена повторная отправка команды после последующих сообщений `READY`.
- Требуется проверка на телефоне после установки нового APK.

# Ручное открытие без RSSI-порогов — 19.08.2026

- Добавлена передача `FORCE_OPEN` для ручной кнопки.
- `assembleDebug` — успешно.
- Проверка на физическом телефоне пока не выполнялась.
# 24.09.2026 — синхронизация рабочего режима

- Исходная debug-сборка: PASS.
- Debug-сборка после изменений: PASS (`build-android.ps1`).
- Debug APK установлен на физическое Android-устройство по Wi-Fi ADB: PASS.
- До установки сохранены `accelerometer_rotation=0`, `user_rotation=0`;
  после установки оба значения восстановлены и повторно прочитаны: PASS.
# 24.09.2026 — имя телефона после pairing

- Исходная debug-сборка: PASS.
- `build-android.ps1` после автоматической записи имени: PASS.
- APK установлен по Wi-Fi ADB: PASS; `accelerometer_rotation=0` и
  `user_rotation=0` восстановлены и повторно прочитаны.
- Аппаратное подтверждение записи имени отложено: BLE Lock не был найден
  телефоном, поэтому GATT-запись не запускалась.

## 24.09.2026 — parser состояния ULP

Исходная и итоговая сборки build-android.ps1 PASS (37 s каждая).
Изменён MainActivity.kt: guard для вывода режима из известного нормального статуса.
APK не устанавливался; аппаратная проверка новых статусов Android не выполнена.

## 25.09.2026 — установка Android

По запросу пользователя установлен app-debug.apk сборки 24.09.2026 23:47
через adb install -r на 2510DRA23E: Success. Данные приложения сохранены.
До установки сохранены accelerometer_rotation=0 и user_rotation=0;
после установки восстановлены и повторным чтением подтверждены 0/0.
Снимок: .device-agent/android-install-orientation-20260925.json.

## 25.09.2026 — номер и время Android-билда

Исходная сборка PASS (23 s), итоговая PASS (46 s), проектным build-android.ps1.
BuildConfig проверен: BUILD_NUMBER=1, BUILD_TIMESTAMP=2026-09-25 01:17:22,
VERSION_CODE=1, VERSION_NAME=1.0.0. Существующие deprecation warnings остаются.
APK установлен adb install -r: Success. Ориентация сохранена и восстановлена,
повторное чтение подтверждает accelerometer_rotation=0/user_rotation=0.
Логи: .device-agent/android-build-info-baseline.log и android-build-info-final.log.
Визуальная проверка на экране телефона не выполнялась.

## 25.09.2026 — передача версии в Web UI

Исходная сборка PASS (27 s), итоговая PASS (30 s). Android build 3,
BUILD_TIMESTAMP=2026-09-25 08:16:36. adb install -r: Success; ориентация 0/0
сохранена и восстановлена с повторным чтением. Главный экран не содержит версии.
URL формируется через Uri.Builder; metadata кодируется во fragment.
Node: PASS синтаксис всего Web JS, корректные metadata, отсутствие metadata,
отклонение некорректных данных. Визуальная проверка браузера не выполнялась.


## 25.09.2026 — EOL hygiene

Baseline build PASS (24 s), итоговая build PASS (37 s), APK build 5 install Success.
Настройки ориентации 0/0 сохранены, восстановлены и повторно проверены.
Изменений логики Android в этом этапе нет; исправлены только смешанные EOL.

## 26.09.2026 — проверка подписи и установка

Проектная debug-сборка PASS. Текущий APK подписан сертификатом корневого
`.android/debug.keystore` (SHA-256 начинается с `1F629C79`); установленная до
переустановки версия была подписана отдельным ключом из
`android_lock_beacon/.android` (SHA-256 начинается с `B22759A3`). APK установлен
после удаления старого пакета: Success. Данные приложения при удалении очищены.
`accelerometer_rotation=0` и `user_rotation=0` сохранены, восстановлены и
повторно проверены. Удаление дублирующего хранилища заблокировано политикой.

## RSSI-порог и свайп-экран настройки — 27.09.2026

- Baseline до изменений: `build-android.ps1` — PASS.
- `testDebugUnitTest` — PASS: 30 тестов, включая 8 `BleScanPolicyTest`;
  добавлены проверки RSSI выше, ровно на и ниже порога.
- Итоговый `build-android.ps1` (`assembleDebug`) — PASS; APK установлен на
  Xiaomi 2510DRA23E через ADB.
- Перед каждой установкой сохранены и восстановлены системные значения
  ориентации; при итоговой установке `accelerometer_rotation=1`,
  `user_rotation=0`, после установки значения совпали.
- Ручная проверка: свайп вверх открыл RSSI-экран; системный Back и свайп
  в сторону вернули главный экран. Ползунок при проверке не перемещался,
  сохранённый порог не менялся.

## Удалён свайп вниз для обновления — 27.09.2026

- Baseline `build-android.ps1` до изменений — PASS.
- Удалён `SwipeRefreshLayout` и его обработчик `pollAdminState()`; ручная
  команда обновления админ-статуса в приложении сохранена.
- Итоговый `build-android.ps1` (`assembleDebug`) — PASS.
- APK установлен через ADB: Success. Перед установкой и после неё проверены
  значения ориентации `accelerometer_rotation=0`, `user_rotation=0`;
  значения восстановлены без изменений.

## Гистерезис активного BLE-соединения — 27.09.2026

- Baseline `build-android.ps1` до изменений — PASS.
- Итоговый `build-android.ps1` (`assembleDebug`) — PASS.
- APK не установлен: `adb devices -l` не показывает подключённого телефона.
  Установка и ручная проверка экрана/разрыва соединения остаются непроверенными
  на устройстве.

## Порог RSSI подключения в архивной версии — 28.09.2026

- Первичная сборка до feature-правок выявила незавершённые ссылки в уже
  находившемся в рабочем дереве `PassiveBleScan.kt`: отсутствовали
  `LockSettings.rssiThreshold` и action/extra-константы сервиса.
- После добавления настройки и compile-time констант проектный скрипт
  `build-android.ps1` прошёл успешно; это подтверждает baseline компиляции
  исправленного исходного дерева.
- Итоговый `build-android.ps1` — PASS (`assembleDebug`).
- Debug APK установлен поверх приложения на Xiaomi 2510DRA23E: `Success`;
  данные приложения сохранены, `MainActivity` запущена.
- До и после установки ориентация `accelerometer_rotation=0`,
  `user_rotation=0`; проверена UI-иерархия: один SeekBar отображается со
  значением `-66 dBm`. В проверенном окне после установки FATAL EXCEPTION нет.
- Unit-тесты и физическая проверка порога RSSI отдельно не запускались.

## Звуковые сигналы BLE — 28.09.2026

- Baseline `build-android.ps1` до изменений — PASS (`assembleDebug`).
- Итоговый `build-android.ps1` после добавления сигналов — PASS
  (`assembleDebug`).
- Unit-тесты не запускались: изменение касается Android audio/GATT callback;
  отдельная тестовая инфраструктура для этого отсутствует.
- APK установлен на Xiaomi 2510DRA23E через `adb install -r`: Success; приложение
  запущено. `accelerometer_rotation=0` и `user_rotation=0` до/после установки
  совпадают. После установки не было новых BLE-событий, поэтому физическое
  прослушивание трёх сигналов ещё не подтверждено.

## Сигнал слабой BLE-рекламы — 28.09.2026

- Baseline `build-android.ps1` до изменений — PASS (`assembleDebug`).
- Итоговый `build-android.ps1` после добавления четвёртого сигнала — PASS
  (`assembleDebug`).
- Повтор слабого сигнала программно ограничен интервалом 1,5 секунды;
  свежие слабые рекламные пакеты после установки зафиксированы в logcat
  (`rssi=-63..-67 dBm`, порог `-61 dBm`). Акустическую слышимость удалённо
  подтвердить нельзя.
- APK установлен на Xiaomi 2510DRA23E; авто-режим остался включён, порог не изменён.
- До и после установки `accelerometer_rotation=1`, `user_rotation=0`.

## Прототип native BLE auto-connect — 28.09.2026

- Baseline `build-android.ps1` до правки: PASS (`assembleDebug`).
- Сборка прототипа: PASS (`assembleDebug`, 26 секунд).
- APK установлен через `adb install -r`; `auto_open_enabled=true`, сохранённый
  BLE-адрес и бонд не очищались. До/после установки значения ориентации
  `accelerometer_rotation=0`, `user_rotation=0` совпадают.
- Фактический native путь в logcat: `EXPERIMENT_AUTO_CONNECT_START` 15:52:44.402;
  Android принял `connectGatt(auto=true)`; `GATT_CONNECTED` 15:52:45.882;
  `READY` 15:52:46.598; запись команды OPEN успешна 15:52:46.717.
- ESP API подтвердил после команды: `state=OPEN`, `connected=true`,
  `encrypted=true`, `bonded=true`, RSSI `-31 dBm`.
- В том же прогоне MIUI зафиксировала `deep doze change, mDeviceIdle: true`
  в 15:54:58.249. После этого до успешного подключения не было события
  `mDeviceIdle: false`; экран оставался выключенным, в `dumpsys power` было
  `mWakefulness=Dozing`, а `dumpsys deviceidle` показывал `mState=IDLE`.
- В 15:56:50.220 начался новый bonded auto-connect; GATT подключился в
  15:56:51.289 (1,07 с), READY наступил в 15:56:52.000, OPEN записан успешно
  в 15:56:52.082. ESP подтвердил `OPEN`, `connected=true`, `encrypted=true`,
  `bonded=true`, RSSI около `-31..-33 dBm`.
- Во время прогона было несколько кратких отключений с GATT status 19, после
  которых штатный reconnect backoff продолжал попытки; к 16:00 соединение было
  установлено и замок оставался открыт. Нужны повторные проходы для оценки
  частоты этих отказов и стабильности, но первый авто-open после зафиксированного
  Deep Doze успешен.
- Сбор logcat выполнялся в `%TEMP%\blelock_native_autoconnect_background.log`.
  `device-agent.ps1 events` не смог прочитать локальный connection state JSON
  (ошибка `Additional text encountered after finished reading JSON content`);
  локальный файл не изменялся, состояние ESP сверялось read-only через HTTP API.

## Защищённая готовность native auto-connect и защита OPEN — 28.09.2026

- `build-android.ps1`: PASS до и после правок; APK установлен на Xiaomi/Redmi 2510DRA23E. Поворот сохранён (`accelerometer_rotation=0`, `user_rotation=0`).
- В 17:36:33.352 получен `GATT_CONNECTED`; в 17:36:34.231 успешно завершено защищённое чтение статуса (`AUTH_SUCCESS`); в 17:36:34.335 получен один `OPEN_SENT`.
- До 17:38:20 GATT RSSI-запросы продолжали выполняться примерно раз в 2 с; повторной команды OPEN и нового status 19 в этом интервале нет.
- ESP status/events подтверждают firmware 10.0.224, зашифрованное bonded-подключение и принятую OPEN. ESP уже в `OPEN_HOLD`, поэтому ход физического механизма и штатное закрывание не подтверждены.
- Unit tests не запускались; выполнены сборка и проверка на физическом телефоне.

## Проверка порога автооткрытия по GATT RSSI — 28.09.2026

- Сборка `build-android.ps1` прошла до и после правок; APK установлен на Xiaomi/Redmi 2510DRA23E.
- Effective threshold из device-protected prefs: `-49 dBm`; активный runtime setting подтверждён строкой службы. CE-копия `-55 dBm` устарела и не читается `LockSettings`.
- GATT_CONNECTED 18:07:58.524; AUTH_SUCCESS 18:07:59.363; GATT RSSI `-49` при пороге `-49` в 18:07:59.450; OPEN_SENT 18:07:59.530. Задержка после GATT_CONNECTED — около 1,01 с. ESP event log подтвердил принятие команды.
- Последующие RSSI-опросы не привели к повторному OPEN. ESP осталась encrypted/bonded/connected в `OPEN_HOLD`, без fault flags; физическое движение замка не проверяется этим режимом.
- ESP config сейчас `phone_rssi_threshold=-50 dBm`, `zone_exit_grace_ms=500 ms`; Android secure settle — 700 ms. Текущий ESP-side RSSI `-38 dBm`, поэтому выдержка разрыва не сработала. Нужен отдельный подход с RSSI около/ниже порога, чтобы проверить ранний разрыв.
- Unit tests не запускались. Значения ориентации при двух установках сохранены и восстановлены: `0/0` и `1/0`.
# 2026-09-28 — исправление режима MainActivity

- Baseline `build-android.ps1`: PASS (`assembleDebug`).
- Сборка после правки: PASS (`assembleDebug`).
- Debug APK установлен на Xiaomi/Redmi `192.168.1.65:41865`.
- Параметры ориентации до/после: `accelerometer_rotation=1`, `user_rotation=0`.
- Аппаратная проверка после установки: обычный bind оставил сервис в `AUTO_OPEN`; GATT подключён 18:27:48.755, `AUTH_SUCCESS` 18:27:49.541, RSSI `-59` достиг порога `-68`, `OPEN_SENT` 18:27:49.693. ESP подтвердила `open=true`, `hold_open=true`, `connected/encrypted/bonded=true`.
# 2026-09-28 — кнопки главного экрана

- Baseline и итоговая `build-android.ps1`: PASS.
- APK сборки 87 установлен на телефон, ориентация `accelerometer_rotation=1`, `user_rotation=0` сохранена.
- UI hierarchy после установки подтвердила видимость всех трёх кнопок и `enabled=true` для обеих кнопок режимов.
- Сервис остался в AUTO. Текущий GATT RSSI `-59 dBm` при пороге `-50 dBm`, поэтому OPEN не отправлялся; ESP остаётся в `OPEN_HOLD`.
# 2026-09-28 — вертикальный ползунок RSSI

- `build-android.ps1` до и после правок: PASS.
- APK установлен на Xiaomi/Redmi 2510DRA23E, настройки поворота экрана сохранены `accelerometer_rotation=0`, `user_rotation=0`.
- Фактическое проведение свайпа/drag после установки отложено: телефон на экране блокировки; блокировку не обходил.

# 2026-09-28 — исправление экрана порога

- Контрольная и итоговая проектные сборки: PASS (`build-android.ps1`).
- APK установлен; ориентация `accelerometer_rotation=1`, `user_rotation=0` восстановлена и проверена.
- Экран и жесты на устройстве вручную не проверены: телефон оставался на системной блокировке.

# 2026-09-28 — удалены кнопки и свайп вниз на главном экране

- `build-android.ps1` до изменений и после изменений: PASS.
- Debug APK установлен. Ориентация сохранена `0/0`.
- Исходники больше не содержат кнопок/ресурсов «Настройки замка», «Закрыть настройки» и «Назад».

# 2026-09-28 — доступность кнопки «Настройки»

- `build-android.ps1` до и после изменения: PASS.
- Установка APK не выполнена: телефон ушёл offline в ADB. Параметры ориентации не изменялись.
- По исходному коду «Настройки» показывается при наличии BLE-разрешений и доступна для запуска соединения; физическая UI-проверка ожидает возврата устройства в ADB.
## 2026-09-28 — уточнение проверки владельца и рекламы

- Подтверждение владельца и BLE-разрешения управляют доступностью кнопки «Настройки»; активная GATT-сессия влияет только на уже открытые admin-действия. Нажатие запускает подключение при необходимости.
- При native auto-connect departure scan теперь принимает рекламу без аппаратного UUID ScanFilter: UUID проверяется в callback, первый пакет за цикл даёт единственный beep.
- APK установлен; значения ориентации сохранены и восстановлены (повторная установка: `accelerometer_rotation=1`, `user_rotation=0`). Телефон оставался заблокирован, поэтому визуальная проверка кнопки и фактическое прослушивание сигнала не выполнены.
## 2026-09-28 — сигнал при выключенном экране

Baseline и итоговая build-android.ps1: PASS. BLE advertisement callbacks поступали при выключенном экране; DND выключен, ring/notification volume не нулевой. APK installation and physical audio verification pending.

Итоговая сборка после добавления native auto-connect tone: PASS. Установка новой сборки и проверка tone при screen-off ожидаются.

Итоговый screen-off тест: APK установлен, ориентация `0/0` восстановлена. В состоянии `mWakefulness=Dozing` получено `Advertisement tone started=true screenOn=false`; `dumpsys power` подтвердил ACQ и REL timed partial wake lock. Сборка `build-android.ps1`: PASS. Проверена передача сигнала в audio system, не физическая слышимость.

## 2026-10-01 — порог RSSI перед подключением

- `build-android.ps1` до изменения: PASS, build 123; после изменения: PASS, build 124. `git diff --check`: PASS.
- REDMI Note 15: build 124 установлен с сохранением данных. Ориентация до установки и после запуска `accelerometer_rotation=0`, `user_rotation=0`; исходные значения сохранены в `artifacts/redmi-note15-rotation-before-rssi-gate-build124-20261001.json`.
- При пороге `-72 dBm` рекламные пакеты `-99`, `-94`, `-86 dBm` оставили телефон в поиске; пакет `-68 dBm` вызвал GATT-подключение. Лог: `artifacts/redmi-note15-rssi-gate-build124-20261001.log`.
- Первый запуск через `monkey` после force-stop сопровождался одним ANR foreground service на HyperOS. Повторный запуск завершился READY и успешной проверкой порога. Длительная работа в фоне отдельно не проверялась.

## 2026-10-01 — scan с фильтром по имени

- Исходная сборка Android build 125: PASS. Итоговая сборка после замены нефильтрованного scan на фильтр по имени: PASS, build 126. Результат проверки на REDMI Note 15 — ниже.
- Build 126 установлен; при погашенном экране получены реклама и READY, но после нескольких циклов HyperOS выдала `scanning too frequently`. После ограничения частоты и watchdog Android build 127: PASS. Физическая проверка build 127 — ниже.
- Build 127 установлен; реклама принималась и READY достигался, но при открытом UI отдельный RSSI scan привёл к повторному `scanning too frequently`. После прекращения этого scan сборка Android build 128: PASS. Проверка на телефоне — ниже.
- Build 128 установлен на REDMI Note 15 с сохранением данных и ориентации `0/0`. В течение более минуты несколько циклов scan/GATT получали рекламу и READY; слабые пакеты оставались ниже порога, новые сообщения `scanning too frequently` не появились. Журнал сохранён в `artifacts/redmi-note15-filtered-scan-build128-20261001.log`.

## 2026-10-01 — возврат телефона на build 122

- APK build 122 установлен через `adb install -r`; SHA-256 установленного `base.apk` совпал с `artifacts/android-main-build122-before-rssi-gate.apk`. Данные приложения сохранены, ориентация до/после `0/0`.
- При погашенном экране журнал показал native auto-connect, GATT_CONNECTED и READY. Проверка возвращения через 30 минут отдельно не выполнялась. Текущие исходники проекта остаются build 128.
## 2026-10-01 — правило одного автооткрытия за присутствие

- Baseline `build-android.ps1`: PASS, build 129. Итоговая сборка: PASS, build 130.
- `build-android.ps1 -Action test`: PASS; обновлённый `AutoOpenPresenceGateTest` проверяет блокировку после OPEN и снятие только после `DEPARTED`.
- Build 130 установлен на REDMI Note 15 через ADB с сохранением данных. Перед установкой сохранены `accelerometer_rotation=0` и `user_rotation=0` в `artifacts/redmi-note15-rotation-before-presence-rule-build130-20261001.json`; после установки оба значения восстановлены и повторно проверены как `0/0`.
- На телефоне получены `NATIVE_AUTO_CONNECT_START`, `GATT_CONNECTED`, `AUTH_SUCCESS` и один `OPEN_SENT` в 13:45:22. ESP32 10.0.248 приняла OPEN и перешла в `OPEN` без fault. Второго OPEN при продолжающемся соединении до 13:46:02 не было. Реальный уход/возврат телефона в этом прогоне не выполнялся.
## 01.10.2026 — административная панель без таймера Б

- Исходный build 133: PASS; после изменений build 134: PASS через `build-android.ps1`.
- APK: `app/build/outputs/apk/debug/app-debug.apk`, 2291236 байт. На телефон не устанавливался, чтобы не вмешиваться в двухчасовой тест BLE Foreva.
- Сохранен GATT-формат из шести числовых полей: пятое поле теперь фиксированный `0`. Сборка проверена; взаимодействие новой Android-панели с физическим телефоном отдельно не проверялось.
- Изменены `app/src/main/java/com/blelock/app/MainActivity.kt`, `app/src/main/res/values/strings.xml`, `AUDIT.md`, `CHANGELOG.md`, `TEST_REPORT.md`.

## 02.10.2026 — кнопка открытия через интернет

- Проектный `build-android.ps1` PASS, debug build 137. Кнопка добавлена на главный экран и отключена до появления обработчика. На физическое устройство эта сборка не устанавливалась.
## 02.10.2026 — Android beacon для новой схемы

- Исходная сборка 138 PASS; после правок `build-android.ps1` PASS, build 139.
- APK пока не установлен. Затронуты `PhoneBeacon.kt` (новый), `BleLockService.kt`, `MainActivity.kt`, `AndroidManifest.xml`.
- Интеграционный тест с ESP32 ожидает реализации scanner/central.
## 02.10.2026 — исправление старого AUTO OPEN

- Build 139 установлен на телефон с сохранением и восстановлением настроек поворота. Его проверка обнаружила старую GATT-команду `OPEN` при запуске UI.
- После исправления `build-android.ps1` PASS, build 140. Новый APK ещё не установлен.
## 02.10.2026 — настройка мощности маяка

- С `ULTRA_LOW` ESP32 видела новый маяк около −77…−80 dBm при пороге −60 dBm. Изменено на `ADVERTISE_TX_POWER_HIGH`.
- `build-android.ps1` PASS, build 141. APK ещё не установлен.
## 02.10.2026 — установка build 141

- Build 141 установлен на физический телефон. Перед установкой значения поворота `0/0` сохранены в `artifacts/android-rotation-before-architecture-build141-install-20261002.json`, затем восстановлены и проверены. `BLUETOOTH_ADVERTISE` предоставлено.
- Сохранённый AUTO сейчас выключен. Новая мощность advertising при штатном RSSI-пороге ESP32 −60 dBm ожидает короткой проверки после включения AUTO пользователем.
## 02.10.2026 — Wi‑Fi управление постоянным открытием

- Проектный `build-android.ps1`: PASS.
- Физический HTTP-вызов и установка этого APK не выполнялись; требуется проверить телефон и ESP в одной доступной локальной сети.
## 02.10.2026 — baseline новой BLE-архитектуры

- `build-android.ps1`: PASS, debug build 143. Физические pairing/AUTO проверки до изменений не проводились.
## 02.10.2026 — режимы Auto Open и Always Open

- `build-android.ps1`: PASS, build 144. `build-android.ps1 -Action test`: PASS.
- Физическая установка не выполнена: ADB устройство не подключено. RF/pairing на этом этапе не тестировались.
## 02.10.2026 — проверка со стороны ESP

- ESP firmware 10.0.268 прошла сборку и OTA-проверку. Проверка связи с новым Android pairing откладывается до восстановления ADB/доступности телефона.
## 02.10.2026 — pairing test status

- Android unit/build проверки этапа 2 прошли на build 144. ESP firmware 10.0.270 установлена; Android 144 не установлена, так как ADB устройство недоступно.
- Приёмочный long-press удаляет все bonds; тест не запускался во избежание потери уже зарегистрированных телефонов без доступного Android для восстановления.

## 02.10.2026 — owner marker / ESP central transaction

- ESP: `build-esp32.ps1` PASS, firmware 10.0.271; OTA PASS, устройство подтвердило переход 10.0.270 → 10.0.271.
- Android: `build-android.ps1` PASS, debug build 146; `build-android.ps1 -Action test` PASS для debug/release unit tests.
- Runtime BLE pairing, запись owner marker на телефоне и UI visibility не проверены: ADB устройство отсутствует. Физические проверки остаются обязательными.

## 02.10.2026 — Web UI обращается по Wi-Fi

- `build-android.ps1`: PASS, debug build 147; `build-android.ps1 -Action test`: PASS (debug/release unit tests).
- UI при отсутствии сохранённого IP просит ввести IPv4; сетевой вызов/браузер и реальная доступность ESP32 с телефона не тестировались без ADB/устройства.

## 02.10.2026 — Android Central lifecycle gate

- `build-android.ps1`: PASS, debug build 148; `build-android.ps1 -Action test`: PASS (debug/release unit tests).
- Проверка `adb devices -l`: список пуст. Установка APK, проверка Auto ON/OFF, background advertising и owner Web UI на физическом Android не выполнялись.

## 02.10.2026 — Android Peripheral lifecycle

- `build-android.ps1`: PASS, build 148; `build-android.ps1 -Action test`: PASS (debug/release unit tests).
- `adb devices -l`: нет подключённых устройств. Boot/restart, Auto ON/OFF и отсутствие Android Central физически не проверены.

## 02.10.2026 — лимит ESP bond store

- `build-esp32.ps1`: PASS, firmware 10.0.272; OTA PASS, Agent API подтвердил переход 10.0.271 → 10.0.272.
- Лимит проверен статически и компиляцией, заполненный до 20 записей bond store тест физически не выполнялся.

## 02.10.2026 — Android foreground lifecycle

- `build-android.ps1`: PASS, debug build 148; `build-android.ps1 -Action test`: PASS (debug/release unit tests).
- Физическая проверка Auto ON/OFF и уведомления ожидает ADB-подключения телефона.

## 02.10.2026 — Android callback guard

- `build-android.ps1`: PASS, build 148; `build-android.ps1 -Action test`: PASS (debug/release unit tests).
- Физический runtime test отсутствует: ADB устройство не подключено.

## 02.10.2026 — Android Wi-Fi provisioning beacon

- `build-android.ps1`: PASS, build 152; `build-android.ps1 -Action test`: PASS (debug/release unit tests).
- Физический hotspot/BLE provisioning не проверен: ADB телефон не виден; ESP пока не реализует protocol byte `0x81`/Wi-Fi GATT read.

## 02.10.2026 — Android↔ESP hotspot provisioning протокол

- Android `build-android.ps1`: PASS, build 152; debug/release unit tests PASS.
- ESP `build-esp32.ps1`: PASS, firmware 10.0.273; OTA PASS (`10.0.272 → 10.0.273`), Agent API подтвердил новую версию.
- End-to-end hotspot, bonded long-read, Wi-Fi DHCP и повторное AUTO не проверены: ADB phone недоступен. ESP runtime parser проверен сборкой; пароль не логируется.
- Android не получает dynamic DHCP IP после разрыва GATT, поэтому автооткрытие URL Web UI остаётся непроверенным/незавершённым.

## Hotspot IPv4 через защищённый PhoneBeacon — Android этап (02.10.2026)

- После `LocalOnlyHotspotCallback.onStarted()` Android пытается определить текущий IPv4 по поднятым AP интерфейсам (`ap*`, `softap*`, `swlan*`); если AP интерфейс не обнаружен — использует единственный однозначный `wlan*` IPv4. При неоднозначности/ошибке IP не передаётся, фиксируется logcat warning, SSID/пароль provisioning продолжаются.
- IP (4 bytes network-order; `00 00 00 00` = отсутствует) передаётся BleLockService в `PhoneBeacon`. В GATT добавлена encrypted-read характеристика `HOTSPOT_IP_UUID = 6f4f0014-9a7b-4c21-8d32-5e6f708192a3` к существующему сервису.
- `PhoneBeacon` проверяет `BOND_BONDED`; `PERMISSION_READ_ENCRYPTED` оставляет фактическую проверку encryption стандартному Android Bluetooth stack. IP очищается при `onStopped`, `onFailed` и остановке reservation.
- Android `build-android.ps1 -Action test`: PASS (debug/release compile и unit tests). Добавлены проверки выбора AP-интерфейса, неоднозначности WLAN-кандидатов и недопустимого IPv4.

## HOTSPOT_IP — завершение Android/ESP (02.10.2026)

- Android IPv4 lookup использует перечисление поднятых интерфейсов и адресов. В первую очередь рассматриваются интерфейсы с AP-названием `ap*`, `softap*`, `swlan*`; единственный `wlan*` кандидат допускается только если нет активной Wi-Fi station сети. Адреса loopback/link-local/multicast/unspecified/broadcast отбрасываются. Неоднозначность возвращает «нет данных» и сохраняет credential/DHCP fallback. Стандартный Android API не раскрывает стабильно имя SoftAP интерфейса, поэтому физическое подтверждение на целевой модели остаётся необходимым.
- GATT characteristic: `HOTSPOT_IP_UUID = 6f4f0014-9a7b-4c21-8d32-5e6f708192a3`, в существующем PhoneBeacon service; 4 bytes IPv4 network-order, `00 00 00 00` означает отсутствие IP.
- Android проверяет `device.bondState == BOND_BONDED`; characteristic имеет `PERMISSION_READ_ENCRYPTED`, так что enforcement шифрования остаётся системному Bluetooth stack. IP очищается по жизненному циклу reservation; session token не даёт позднему STOP старого hotspot очистить адрес новой сессии.
- ESP использует единственный исходящий GATT connection: `connect → encrypted/bonded proof BLE_LOCK_V1 → Wi-Fi credentials → HOTSPOT_IP`. IP читается только при provisioning marker `0x81`; обычный AUTO proof не делает дополнительного чтения и соединения.
- ESP проверяет unicast IPv4, хранит его только в RAM через `wifi_ota_set_runtime_phone_ip()` / `wifi_ota_get_runtime_phone_ip()`. На IP отсутствует/ошибке продолжается credential provisioning fallback. ESP DHCP/static-IP поведение не менялось; NVS не получает IP телефона.
- Android `build-android.ps1 -Action test`: PASS, build 153 (debug/release и unit tests). ESP `build-esp32.ps1`: PASS, firmware 10.0.275; OTA PASS, device-agent подтвердил `10.0.274 → 10.0.275`.
- Радио-тест характеристик на физическом телефоне и проверка фактического интерфейса hotspot не выполнялись. Resolver покрыт unit tests, но использует доступные интерфейсы Android и при ambiguity намеренно возвращает отсутствие адреса.

## Hotspot provisioning race fix — 02.10.2026

- `build-android.ps1 -Action test`: PASS, debug/release compile и unit tests, build 154.
- Исходный радиопрогон был на установленном build 153: hotspot `AndroidShare_5591`, обычное AUTO подтверждение ESP и timeout примерно через 60 секунд. Промежуточный build 154 установили до последней lifecycle правки. Итоговый APK build 155 собран отдельно через `build-android.ps1 -Action debug`; SHA-256 `A7CB26C74B0E5C4E9ED486A83D7A94BB7F7F4CFF1F3D9814660E8AC64C3ECDB5`. Адрес AP IPv4/успешный provisioning не подтверждены.
- Финальный APK build 155 не установлен: ADB wireless install не завершилась, телефон заблокирован; разблокировка нужна для завершения wireless ADB install. Rotation values после попыток проверены: `accelerometer_rotation=1`, `user_rotation=0`.
- Базовая ESP сборка firmware 10.0.276: PASS. Обязательная OTA upload/reboot verification: FAIL — Agent API `192.168.1.54` не вернулся за 60 секунд. Установленная версия не подтверждена; ESP код этой правкой не менялся.
- End-to-end критерии ожидают повтор после разблокировки телефона и возврата ESP в сеть: provisioning marker, encrypted reads `WIFI_SETUP` + `HOTSPOT_IP`, совпадение адреса hotspot, DHCP ESP и доступность Web UI.





## 2026-10-04 — Web UI только через hotspot телефона

- `build-android.ps1`: PASS, debug build 183.
- Установка APK и runtime-проверка этого изменения отложены до сборки/прошивки ESP с раздельной маршрутизацией UI и Agent API.
- В этом этапе APK не устанавливался; системные значения ориентации экрана не менялись.

## 2026-10-04 — локальные HTTP маршруты и попытка hotspot provisioning

- ESP 10.0.316 на домашней сети: `/` вернул 404, `/api/agent/status` и `/api/agent/monitor` вернули HTTP 200.
- Android build 183 установлена; исходные настройки ориентации `0/0` после установки восстановлены и повторно проверены.
- Hotspot телефона поднялся (`172.18.51.2`), но ESP не выбрала BLE provisioning маяк: наблюдавшийся RSSI −63…−66 dBm ниже порога −60 dBm. Watchdog корректно остановил hotspot через 60 секунд.
- Runtime подтверждение DHCP/IP, открытия браузера через hotspot и возврата ESP к домашней сети после закрытия hotspot остаётся незавершённым.

## 2026-10-04 — end-to-end подтверждение на firmware 10.0.317

- Исправлен ESP BLE filter: `0x81` Wi-Fi provisioning и `0x82` Web UI IP request проходят даже если обычный beacon того же device ID ранее попал в suppression после proof `status=257`.
- На Redmi Note 15 нажатие Settings создало LocalOnlyHotspot `172.18.51.2`; encrypted bonded provisioning передал credentials, затем DHCP и защищённая IP характеристика вернули ESP адрес `172.18.51.177`.
- Chrome открыл Web UI; страница отобразила firmware 10.0.317 и настройки.
- Выход из Chrome остановил hotspot; Agent API вернулся по домашней сети `192.168.1.54`. LAN проверки: `/` → 404, `/api/agent/status` → 200, `/api/agent/monitor?since=0` → 200.
- Ориентация экрана после установки Android build 183 восстановлена и проверена: `accelerometer_rotation=0`, `user_rotation=0`.

## 2026-10-04 — адаптивный обратный таймер кнопки Settings

- Android baseline build 184: PASS.
- Изменение собрано: Android build 185 PASS.
- APK build 185 установлена, значения ориентации до/после установки совпали `accelerometer_rotation=0`, `user_rotation=0`.
- Устройство имеет persisted countdown `1` sec.; default `6` sec. используется при отсутствии значения. Физический цикл повторного нажатия и обновление значения при новом успешном открытии после build 185 не выполнялись.

## 2026-10-04 — физическая проверка адаптивного таймера Settings

- PASS: Android build 185 установлен на Redmi Note 15; перед установкой и после неё `accelerometer_rotation=0`, `user_rotation=0`.
- PASS: до первого проверочного нажатия приложение хранило 1 сек. После нажатия Chrome открыл `http://172.18.51.177/` примерно через 7,3 сек.; сохранённое значение стало 8 сек. (ceil).
- PASS: при повторном нажатии таймер использовал предыдущие 8 сек. — UI-снимок во время отсчёта показывает оставшиеся 5 сек. После следующего открытия URL сохранённое значение стало 5 сек.
- Артефакты: `artifacts/adaptive-settings-timer-build185-next-countdown.png`, `artifacts/adaptive-settings-timer-build185-next-countdown.xml`, `artifacts/adaptive-settings-timer-build185-second-open.png`.
- Итог: адаптивный таймер подтверждён физически. Хранилище — Android device-protected SharedPreferences; default 6 сек. применяется только при отсутствии сохранённого значения. 60-секундный hotspot watchdog отдельный и не менялся.
## 2026-10-04 — проверка кнопок и ползунка мощности BLE

- PASS: исходная Android-сборка `build-android.ps1` завершилась до изменений.
- PASS: итоговая Android-сборка `build-android.ps1`, build 188; Kotlin и ресурсы успешно скомпилированы и APK собран.
- PASS: `git diff --check` для изменённых Android исходников и ресурсов.
- Не выполнялись: unit-тесты, установка APK и физическая проверка рекламной мощности; Android runtime не запускался в этой задаче.
- Изменены: `MainActivity.kt`, `LockSettings.kt`, `PhoneBeacon.kt`, `BleLockService.kt`, `VerticalRssiSlider.kt`, `strings.xml`, `android_lock_beacon/BUILD_NUMBER`; обновлены `AUDIT.md`, `CHANGELOG.md`, `TEST_REPORT.md` в корне и `android_lock_beacon`.
- ESP32 исходники не менялись, ESP-сборка не требовалась.
## 2026-10-04 — установка Android build 188 на телефон

- PASS: ADB подключён к `192.168.1.65:37521`; Android сообщил об успешной установке APK (`Success`).
- PASS: пакет `com.blelock.app` присутствует на устройстве (`pm path` вернул APK path).
- PASS: прочитанные перед установкой `accelerometer_rotation=0` и `user_rotation=0` восстановлены и повторно прочитаны как `0` и `0`.
- Устройство: модель `2510DRA23E`; APK build 188, SHA-256 записан в `artifacts/android-rotation-before-beacon-tx-power-build188-install-20261004.json`.
## 04.10.2026 — подпись Сборка 195

- На экране Android оставлена одна строка Сборка <номер>; версия приложения и дата сборки из блока удалены.
- Проверка: baseline build 194 PASS; итоговый build 195 PASS через build-android.ps1, проверен diff подписи и единственный аргумент BUILD_NUMBER. Установка APK и физическая проверка не выполнялись.
- Изменённые файлы: android_lock_beacon/app/src/main/java/com/blelock/app/MainActivity.kt, android_lock_beacon/app/src/main/res/values/strings.xml, android_lock_beacon/BUILD_NUMBER; корневые и Android AUDIT.md, CHANGELOG.md, TEST_REPORT.md.


## 04.10.2026 — установка Android сборки 195

- Телефон 2510DRA23E найден по прежнему IP с новым ADB-портом 192.168.1.65:42223.
- adb install -r: Success. Установлена APK сборки 195 с подписью Сборка <номер> без версии и даты.
- accelerometer_rotation/user_rotation сохранены перед установкой и восстановлены; повторное чтение подтвердило 0/0 → 0/0. Артефакт: artifacts/android-rotation-before-build195-install.json.
- Изменены только записи корневых и Android CHANGELOG.md/TEST_REPORT.md; повторная сборка для установки готовой APK не выполнялась. Проверка UI на экране не выполнялась.


## 04.10.2026 — дата и время сборки восстановлены, APK 197

- Исправлена ошибочная интерпретация запроса: на экране Сборка <номер> и исходная строка Собрано: <дата и время>. Версия приложения скрыта.
- baseline 196 PASS, итоговая build-android.ps1 197 PASS; adb install -r Success на 2510DRA23E, SHA-256 установленной APK совпадает с собранной.
- Ориентация сохранена перед установкой и восстановлена, проверка 0/0 → 0/0. Артефакт artifacts/android-rotation-before-build197-install.json. Физическая проверка UI не выполнялась.
- Изменены MainActivity.kt, android_lock_beacon/BUILD_NUMBER, корневые и Android AUDIT.md/CHANGELOG.md/TEST_REPORT.md.

