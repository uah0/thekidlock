# Пакет Google Play Console — TheKidLock 1.0.0

Дата подготовки: 26.08.2026.

Основание: ветка `feature/character-controls`, commit
`fc56d9efd2a1dbc5ab3dca5a953fdc8368089709` (`Prepare Android release candidate`).

Документ подготовлен по фактическим `app/build.gradle.kts`, `AndroidManifest.xml`,
зависимостям и Android-коду указанного commit. Старые описания PR не использовались
как источник сведений о функциональности.

## 1. Подтверждённые характеристики релиза

- Application ID: `com.blelock.app`.
- Version code: `1`.
- Version name: `1.0.0`.
- `minSdk=26`, `targetSdk=36`, `compileSdk=36`.
- Требуется Android-устройство с Bluetooth LE.
- Нет аккаунта, логина, облачного backend, аналитики, рекламы, покупок и платёжных SDK.
- Сторонние runtime-зависимости ограничены AndroidX Core и SwipeRefreshLayout.
- Настройки приложения хранятся локально в private `SharedPreferences` с
  `allowBackup=false`.
- Управление замком выполняется напрямую по bonded BLE с LE Secure Connections.
- Локальный Web UI открывается во внешнем браузере по IP ESP32; WebView в приложении нет.
- Privacy Policy открывается во внешнем браузере через `ACTION_VIEW`.
- Foreground service объявлен как `connectedDevice`, не экспортирован и показывает
  постоянное уведомление с действием остановки.

## 2. Data Safety

### Условно рекомендуемые ответы в Play Console

Ответ **No data collected or shared** рекомендуется только при подтверждении
владельцем, что production-сборка и production-окружение не содержат telemetry,
analytics, SDK data collection, backend-сбора или иных механизмов сбора и передачи
пользовательских данных, отсутствующих в проверенном коде и release artifact.

1. **Does your app collect or share any of the required user data types? — No.**
2. **Is all of the user data collected by your app encrypted in transit? — Not
   applicable**, поскольку в форме не заявляется collection. Если Console всё же
   показывает этот вопрос, не заявлять общий серверный сбор; прямой BLE-канал с
   замком защищён стандартным BLE bonding/LE Secure Connections.
3. **Do you provide a way for users to request that their data is deleted? — Not
   applicable / No data is collected**, в зависимости от фактической формулировки
   формы. Аккаунтов разработчика и серверных пользовательских данных нет.
4. **Independent security review — No**, если владелец отдельно не проходил MASA.
5. Ответ по **Play Families Policy** зависит от окончательно выбранной владельцем
   целевой аудитории и не должен заполняться до этого решения.

### Обоснование

- `auto_open_enabled`, выбранный BLE address, признаки показа системных подсказок и
  длительность Web UI flow остаются в private storage телефона.
- Приложение не получает координаты. Legacy location permission ограничен
  `maxSdkVersion=32` и нужен старым Android для BLE scanning; на Android 13+ scan
  помечен `neverForLocation`.
- BLE address используется локально для поиска совместимого замка и не отправляется
  разработчику.
- Имя телефона, команды замка и временные Wi-Fi credentials передаются только
  непосредственно выбранному физическому ESP32 по bonded encrypted BLE. Разработчик
  и промежуточные серверы доступа к этому трафику не имеют.
- OTA SSID/password/URL передаются непосредственно замку по BLE только по действию
  администратора; приложение не отправляет их в developer backend.
- Локальная точка Android создаётся через `LocalOnlyHotspot`; ESP32 подключается к
  ней, а локальный URL открывается внешним браузером.
- Production Privacy Policy также открывается внешним браузером; приложение не
  управляет браузерным содержимым.
- В release нет Firebase, Crashlytics, Google Analytics, Ads SDK или собственного
  telemetry endpoint.

Google определяет collection как передачу пользовательских данных с устройства,
но отдельно исключает on-device processing и данные, передаваемые end-to-end так,
что их могут прочитать только отправитель и получатель. Это соответствует
локальному хранению и прямому защищённому BLE-каналу данного релиза.

Без подтверждения отсутствия production telemetry/analytics, SDK data collection,
backend-сбора и иных меняющих декларацию production-практик ответ **No data
collected or shared** считать неподтверждённым.

## 3. Foreground Service declaration — connectedDevice

### Выбор в форме

- Foreground service type: **Connected device**.
- Use case: **Continuous data transfer to an external device** или ближайший
  доступный вариант для непрерывного взаимодействия с Bluetooth-устройством.

### Готовый английский текст

> TheKidLock uses a connected-device foreground service only to maintain the
> user-enabled Bluetooth Low Energy connection with a compatible ESP32 door lock.
> When the user enables Automatic mode, the service scans for the bonded lock,
> reconnects when necessary, monitors BLE signal strength, and sends the protected
> lock command required for proximity-based access. The service must continue when
> the app is in the background or the screen is off; interrupting it would stop the
> explicitly enabled automatic lock function. Android displays an ongoing
> notification titled “TheKidLock is running in the background” with a Stop action.
> The service can restart after device boot only when the user previously enabled
> Automatic mode. It does not collect location, upload data, display ads, or contact
> a developer server.

### Сценарий короткого демонстрационного видео

Рекомендуемая длина: 45–75 секунд, один непрерывный ролик, интерфейс и уведомление
читаемы. Ссылка должна быть доступна reviewer без логина, например unlisted YouTube.

1. Показать телефон рядом с подготовленным bonded ESP32 BLE Lock и главный экран TheKidLock.
2. Показать, что замок найден и кнопка Settings доступна.
3. Нажать **Automatic mode**.
4. Открыть notification shade и крупно показать ongoing notification TheKidLock и
   действие **Stop**.
5. Вернуться на Home или погасить экран на несколько секунд.
6. Второй камерой или в одном кадре показать, что при приближении телефон продолжает
   взаимодействовать с физическим замком в фоне.
7. Разбудить телефон, снова открыть TheKidLock и показать сохранённое соединение.
8. Выключить Automatic mode и показать, что автоматическая фоновая функция отключена.

Не включать в видео пароли Wi-Fi, MAC-адреса целиком, ключи, персональные уведомления
или фактический OTA update.

## 4. Ads declaration

- **Does your app contain ads? — No.**

В коде и зависимостях нет рекламных SDK, banner/interstitial/native/house ads,
платного product placement или рекламной аналитики.

## 5. Target audience and content

### OWNER DECISION REQUIRED

Фактический код не определяет целевую возрастную аудиторию. Владелец должен выбрать
возрастные группы в Play Console. Управление физическим дверным замком и наличие
административных функций могут быть основанием рассмотреть **Ages 18 and over**, но
это не является окончательным решением данного документа.

Параметр **Restrict minor access** также должен быть отдельно выбран владельцем;
значение **Yes** не считается заранее утверждённым. Ответ по Play Families Policy
должен соответствовать окончательному выбору возрастных групп.

## 6. Content rating questionnaire

Тип приложения: **Utility / Productivity / Other non-game application** — выбрать
ближайший вариант, который показывает текущая версия IARC questionnaire.

Ожидаемые ответы по фактическому содержимому APK:

| Вопрос/категория | Ответ |
| --- | --- |
| Violence, graphic violence, weapons | No |
| Fear or horror themes | No |
| Sexual content or nudity | No |
| Profanity or crude humor | No |
| Alcohol, tobacco or drugs | No |
| Gambling, simulated gambling or contests | No |
| User-generated content | No |
| Users communicate or exchange content | No |
| Sharing precise location with other users | No |
| Unrestricted web access inside the app | No |
| Digital purchases or paid random items | No |
| In-app advertising | No |
| News content | No |

Внешний браузер запускается только для production Privacy Policy и локального Web
UI ESP32. Встроенного общего web browser/WebView нет. Ожидаемый результат — низший
возрастной рейтинг уровня Everyone/PEGI 3, но окончательный рейтинг назначает IARC.

## 7. App access

### Выбор в Play Console

- **All or some functionality is restricted**.
- Причина ограничения: для основной функциональности требуется физический
  совместимый ESP32 BLE Lock и выполненный BLE bonding. Логина или платного аккаунта нет.

### Что необходимо предоставить reviewer

Обычный reviewer не сможет полноценно проверить lock controls без оборудования.
Владелец должен согласовать один из вариантов:

1. отправить Google доступный тестовый ESP32/замок по процедуре review; или
2. предоставить удалённо управляемый тестовый стенд и точные инструкции доступа,
   если Google принимает такой вариант; или
3. заранее согласовать с Play support аппаратно-зависимую проверку и приложить
   демонстрационное видео.

Видео полезно, но не следует обещать, что оно всегда заменяет фактический доступ к
hardware: окончательное решение принимает Google review.

## 8. Reviewer instructions — English

> TheKidLock is a companion application for a physical ESP32-S3 Bluetooth Low Energy
> door lock. No user account, login, subscription, cloud service, or developer
> backend is required. The app can be launched and its main screen and Privacy
> Policy can be reviewed without hardware, but BLE control, Automatic mode,
> administrator Settings, LocalOnlyHotspot, and the local Web UI require a compatible
> test lock that has been bonded with the review phone.
>
> Test prerequisites:
> 1. An Android phone with Bluetooth and Wi-Fi enabled.
> 2. A compatible ESP32 BLE Lock test unit running the approved firmware.
> 3. The review phone must be registered with the test lock using the lock's
>    60-second physical registration window.
>
> Test steps:
> 1. Install and open TheKidLock.
> 2. Grant Nearby devices/Bluetooth, Nearby Wi-Fi devices, and notification
>    permissions when requested.
> 3. Place the ESP32 lock into its physical registration mode and complete Android
>    BLE pairing. The app uses standard LE Secure Connections and bonding.
> 4. Confirm that the app discovers and reconnects to the bonded lock.
> 5. Tap “Automatic mode” to enable background proximity operation. Confirm the
>    ongoing connected-device foreground-service notification, then turn the screen
>    off briefly and verify that the service remains active.
> 6. Reopen the app and disable Automatic mode.
> 7. Tap “Keep door open” to test the persistent-open command, then tap it again to
>    return the lock to normal operation.
> 8. Tap “Settings.” The phone creates an Android LocalOnlyHotspot, securely sends
>    its temporary credentials to the ESP32 over bonded BLE, and opens the ESP32's
>    local HTTP Web UI in the default browser. Internet access is not required for
>    this local page. Do not start an OTA firmware update during review unless an
>    explicitly approved firmware artifact has been supplied.
> 9. Return to the app and tap “Privacy Policy.” The default browser should open
>    https://uah0.github.io/thekidlock/privacy/.
>
> The app does not contain ads or analytics and does not upload lock, Wi-Fi, BLE,
> or user data to the developer. Please use the support contact `support@thekid.uk`
> if physical test hardware or additional review coordination is required.

## 9. Store listing — English (US)

### App name

`TheKidLock`

### Short description

`Secure BLE control and automatic access for a compatible ESP32 lock`

Длина не превышает лимит 80 символов.

### Full description

> TheKidLock is the companion app for a compatible ESP32-S3 Bluetooth Low Energy
> door lock.
>
> Connect your phone using standard Bluetooth LE Secure Connections and bonding,
> then control the lock directly without a cloud account or developer server.
>
> Main features:
> • Automatic proximity-based access while an ongoing Android notification keeps
>   the connected-device service visible and under your control.
> • A keep-open mode for situations where the door should remain unlocked until
>   you turn the mode off.
> • Automatic reconnection to a previously bonded lock.
> • Administrator settings for authorized owners.
> • A local ESP32 Web UI opened through an Android LocalOnlyHotspot.
> • Local Wi-Fi and OTA configuration for compatible lock firmware.
>
> TheKidLock stores its app preferences locally on the phone. It does not contain
> ads or analytics and does not require a user account. Communication with the lock
> uses the standard Android Bluetooth security model; the app does not implement a
> custom encryption protocol.
>
> Important: A compatible ESP32 BLE Lock device is required for lock-control,
> automatic, administrator, and local Web UI features. Availability and installation
> of the physical lock hardware are separate from this app.

### Contact and policy

- Support email: `support@thekid.uk`.
- Privacy Policy: `https://uah0.github.io/thekidlock/privacy/`.
- Website: not established by code; owner input required if desired.
- Phone: optional; owner input required if desired.

## 10. Graphic assets

### Required to publish the phone listing

| Asset | Exact requirement |
| --- | --- |
| Play Store app icon | 512 × 512 px, 32-bit PNG with alpha, maximum 1024 KB |
| Feature graphic | 1024 × 500 px, JPEG or 24-bit PNG without alpha |
| Phone screenshots | Minimum 2; JPEG or 24-bit PNG without alpha; each dimension 320–3840 px; longest side no more than 2× shortest side |

### Recommended set

- 4–6 portrait phone screenshots at **1080 × 1920 px (9:16)** for recommendation
  eligibility. Suggested screens: main controls, automatic mode with notification,
  admin/settings, Local Web UI in browser, Privacy Policy.
- Up to 8 screenshots may be uploaded per supported device type.
- Add concise alt text (up to 140 characters) for every image.
- If tablets/Chromebooks are included in distribution, provide at least four
  large-screen screenshots per supported form factor, 1080–7680 px, 9:16 portrait
  or 16:9 landscape.
- A public or unlisted FGS demonstration video link is required for the foreground
  service declaration; a separate Store preview video is optional.

Не использовать в listing изображения старого character overlay, поскольку exact
release candidate использует стандартные три кнопки без персонажей и фонового JPEG.

## 11. Дополнительные App content ответы

- Government app: **No**.
- Financial features: **No**.
- Health features: **No**.
- News app: **No**.
- COVID/contact tracing: **No**.
- Account creation: **No**.
- In-app purchases/subscriptions: **No**.
- Ads: **No**.
- Privacy Policy URL: `https://uah0.github.io/thekidlock/privacy/`.

## 12. Что должен решить или предоставить владелец

Ниже перечислено то, что невозможно достоверно определить только из APK/репозитория:

1. Юридическое имя developer/publisher и публичные контактные данные.
2. Точные возрастные группы Target audience; **Ages 18 and over** не считается
   окончательно выбранным вариантом.
3. Отдельное решение, включать ли **Restrict minor access**; значение **Yes** не
   считается заранее утверждённым.
4. Ответ по Play Families Policy в соответствии с выбранными возрастными группами.
5. Категория Store listing; рекомендуется **Tools**, но это маркетинговое решение.
6. Бесплатное или платное распространение, страны/регионы и доступность hardware.
7. Права на название, иконку, feature graphic, screenshots и иные brand assets.
8. Готовые локализованные графические материалы и языки listing.
9. URL доступного без логина foreground-service demonstration video.
10. Способ предоставить Google reviewer физический ESP32 BLE Lock или согласованный
   удалённый тестовый стенд.
11. Точная reviewer firmware version и утверждённый firmware artifact, если Google
    потребует проверить OTA. Без него OTA запускать нельзя.
12. Подтверждение отсутствия production telemetry/analytics, SDK data collection,
    backend-сбора и иных операционных механизмов сбора/передачи данных. Только после
    этого **No data collected or shared** остаётся рекомендуемым ответом Data Safety.
13. Наличие независимого security review/MASA — по коду подтвердить нельзя.
14. Play App Signing, developer account verification, tax/payment profile и
    организационные настройки Console.
15. Итоговые release notes и staged rollout plan.
16. Необходимость отдельного support website и номера телефона.

## 13. Официальные источники Google

- Data Safety и определения collection/sharing:
  https://support.google.com/googleplay/android-developer/answer/10787469
- App content, Ads, App access и Privacy Policy:
  https://support.google.com/googleplay/android-developer/answer/9859455
- Foreground service declarations:
  https://support.google.com/googleplay/android-developer/answer/13392821
- Target audience:
  https://support.google.com/googleplay/android-developer/answer/9867159
- Content ratings:
  https://support.google.com/googleplay/android-developer/answer/9859655
- Store preview assets:
  https://support.google.com/googleplay/android-developer/answer/9866151

Документ является подготовительным пакетом, а не отправленной декларацией. Перед
публикацией владелец отвечает за сверку ответов с фактическими production-процессами
и текущими полями Play Console.
