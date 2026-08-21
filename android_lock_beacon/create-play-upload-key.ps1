param(
    [string]$Alias = "ble-lock-upload"
)

$ErrorActionPreference = "Stop"
$keytool = (Get-Command keytool -ErrorAction Stop).Source
$keystorePath = Join-Path $PSScriptRoot "ble-lock-upload-key.jks"
if (Test-Path -LiteralPath $keystorePath) {
    throw "Ключ уже существует: $keystorePath"
}

$storePassword = Read-Host "Пароль хранилища" -AsSecureString
$keyPassword = Read-Host "Пароль ключа" -AsSecureString
$storePlain = [Runtime.InteropServices.Marshal]::PtrToStringBSTR(
    [Runtime.InteropServices.Marshal]::SecureStringToBSTR($storePassword))
$keyPlain = [Runtime.InteropServices.Marshal]::PtrToStringBSTR(
    [Runtime.InteropServices.Marshal]::SecureStringToBSTR($keyPassword))
try {
    & $keytool -genkeypair -v -keystore $keystorePath -alias $Alias -keyalg RSA `
        -keysize 4096 -validity 10000 -storepass $storePlain -keypass $keyPlain `
        -dname "CN=BLE Lock, OU=Mobile, O=BLE Lock, L=, ST=, C=UA"
    if ($LASTEXITCODE -ne 0) { throw "keytool завершился с кодом $LASTEXITCODE" }
} finally {
    $storePlain = $null
    $keyPlain = $null
}

Write-Host "Ключ создан: $keystorePath"
Write-Host "Заполните android_lock_beacon/keystore.properties по примеру keystore.properties.example."
