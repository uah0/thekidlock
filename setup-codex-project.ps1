[CmdletBinding()]
param(
    [string]$ProjectRoot = "C:\Dev\ble-lock-project"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $ProjectRoot)) {
    throw "Каталог проекта не найден: $ProjectRoot"
}

$backupDir = Join-Path $ProjectRoot ("_codex_setup_backup_" + (Get-Date -Format "yyyyMMdd_HHmmss"))
New-Item -ItemType Directory -Path $backupDir | Out-Null

$files = @("AGENTS.md","build-esp32.ps1","build-android.ps1")
foreach ($name in $files) {
    $path = Join-Path $ProjectRoot $name
    if (Test-Path $path) {
        Copy-Item $path (Join-Path $backupDir $name)
    }
}

$sourceDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Copy-Item (Join-Path $sourceDir "AGENTS.md") (Join-Path $ProjectRoot "AGENTS.md") -Force
Copy-Item (Join-Path $sourceDir "build-esp32.ps1") (Join-Path $ProjectRoot "build-esp32.ps1") -Force
Copy-Item (Join-Path $sourceDir "build-android.ps1") (Join-Path $ProjectRoot "build-android.ps1") -Force

Write-Host "Готово."
Write-Host "Резервная копия предыдущих файлов: $backupDir"
Write-Host ""
Write-Host "Проверка ESP32:"
Write-Host "  cd `"$ProjectRoot`""
Write-Host "  .\build-esp32.ps1"
Write-Host ""
Write-Host "Проверка Android:"
Write-Host "  .\build-android.ps1"
