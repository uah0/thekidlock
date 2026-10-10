# Состояние после очистки — 10.10.2026

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
