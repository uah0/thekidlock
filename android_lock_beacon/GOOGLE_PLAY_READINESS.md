# Готовность BLE Lock к Google Play

Дата локальной проверки: 25.08.2026.

Проверенный commit: `f3488a11fa95ff9da5285933da4086c210994106`, ветка
`feature/character-controls`.

## Подтверждено локально

- Стандартный интерфейс Android используется без character overlay и фонового JPEG.
- `compileSdk = 36`, `targetSdk = 36`, `minSdk = 26`.
- `applicationId = com.blelock.app`, `versionCode = 1`, `versionName = 1.0.0`.
- Release включает R8/resource shrinking и не содержит diagnostic logging.
- Создан локальный production upload key: PKCS#12, RSA 4096,
  `SHA256withRSA`; keystore, пароли и `keystore.properties` исключены из Git.
- SHA-256 upload-сертификата:
  `1D:89:FD:F9:C7:32:70:43:41:30:C8:23:10:C4:4F:F9:12:9D:3B:B0:64:14:FE:C3:1F:47:03:EF:B2:6B:46:3B`.
- Чистый запуск `lintRelease bundleRelease assembleRelease assembleDebug` успешен.
- Release APK и AAB подписаны одним production upload-сертификатом.
- Нижняя строка Privacy Policy открывает окончательный production HTTPS URL:
  `https://uah0.github.io/lock-ble/privacy/`.
- Release manifest: `allowBackup=false`, debug/test flags отсутствуют,
  `BleLockService` не экспортирован и использует `connectedDevice`, MainActivity —
  единственная экспортированная Activity.
- В release-артефактах не обнаружены signing secrets, machine paths,
  `keystore.properties`, character overlay или diagnostic markers.
- Подписанный release APK чисто установлен на Xiaomi `2510DRA23E`, Android 16/API 36.
- Exact release candidate прошёл аппаратный regression: clean launch, permissions,
  BLE и сохранённый bond, automatic, always-open, settings/admin, foreground
  service, screen-off/background, relaunch, reconnect, LocalOnlyHotspot/Web UI и
  Privacy Policy — PASS; crash/ANR не обнаружены.
- LocalOnlyHotspot проверен в штатном режиме STA + AP: `wlan0` может оставаться в
  сети `Cisco`, одновременно `ap0` принадлежит `com.blelock.app`, ESP32 получает
  подключение и Web UI открывается по локальному IP.

## Оставшиеся блокеры

- Фактический OTA update не выполнялся по ограничению теста. Для Internal testing
  это не блокирует подтверждённый безопасный вход в admin/Web UI, но перед
  production rollout потребуется отдельный утверждённый firmware artifact и
  контролируемая OTA-проверка.
- В Play Console остаются Data safety, FGS declaration/video, Ads, target audience,
  content rating, reviewer access, store listing, support contact, Play App Signing
  и Internal testing.
- PR 9 содержит устаревшее описание character controls; менять PR без отдельного
  указания не требуется. Issue 11 не закрыт.

## Перед публикацией

Следующий допустимый этап — загрузка уже проверенного AAB в Internal testing после
заполнения обязательных разделов Play Console. Merge, tag и загрузка в Google Play
без отдельного разрешения запрещены.
