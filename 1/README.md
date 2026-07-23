# BLE Lock Android

Android-клиент для ESP32 BLE Lock.

- Kotlin, Android Views.
- minSdk 26, target/compileSdk 35.
- JDK 17 обязателен.
- Android SDK: задаётся через `local.properties`.

Сборка в текущей среде:

```powershell
$env:JAVA_HOME='C:\jdk17'
$env:Path="C:\jdk17\bin;$env:Path"
.\build-local.ps1
```

Этап A3 добавляет Foreground Service, постоянное уведомление, восстановление
после boot, повторное подключение и автоматическую отправку OPEN после
подтверждённого ухода/нового приближения. Перед первичной регистрацией нужно
открыть 60-секундное окно в Web UI ESP32.
