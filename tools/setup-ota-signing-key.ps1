[CmdletBinding()]
param([string]$IdfVersion = "v5.4.4")

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$KeyPath = Join-Path $ProjectRoot ".security\ota_signing_key.pem"

if (Test-Path -LiteralPath $KeyPath) {
    throw "Ключ уже существует и не будет перезаписан: $KeyPath"
}

New-Item -ItemType Directory -Path (Split-Path -Parent $KeyPath) -Force | Out-Null
Push-Location $ProjectRoot
try {
    & eim run "idf.py secure-generate-signing-key $KeyPath" $IdfVersion
    if ($LASTEXITCODE -ne 0) { throw "Не удалось создать ключ подписи (код $LASTEXITCODE)." }
} finally {
    Pop-Location
}

Write-Host "Создан закрытый RSA-3072 ключ: $KeyPath"
Write-Host "Не добавляйте его в Git и не прикладывайте к релизу. Для GitHub Actions сохраните точное содержимое в секрете OTA_SIGNING_KEY_PEM."
