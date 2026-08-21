$ErrorActionPreference = "Stop"

$Source = "$env:LOCALAPPDATA\Android\Sdk"
$Destination = "C:\Dev\AndroidSdk"

if (-not (Test-Path $Source)) {
    throw "Исходный Android SDK не найден: $Source"
}

New-Item -ItemType Directory -Path $Destination -Force | Out-Null

robocopy $Source $Destination /E /COPY:DAT /R:1 /W:1

if ($LASTEXITCODE -ge 8) {
    throw "robocopy завершился с ошибкой, код: $LASTEXITCODE"
}

$probe = Join-Path $Destination "build-tools\35.0.0\core-lambda-stubs.jar"
if (-not (Test-Path $probe)) {
    throw "Проверочный файл не найден после копирования: $probe"
}

Write-Host "Android SDK скопирован в $Destination"
Write-Host "Проверка core-lambda-stubs.jar: OK"
