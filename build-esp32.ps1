[CmdletBinding()]
param(
    [ValidateSet("build","fullclean","flash","monitor","flash-monitor")]
    [string]$Action = "build",
    [string]$IdfVersion = "v5.4.4",
    [string]$Port = "COM3"
)

$ErrorActionPreference = "Stop"

$ProjectRoot = $PSScriptRoot
$EimStateDir = Join-Path $ProjectRoot ".eim-codex"
$EimLog = Join-Path $EimStateDir "eim.log"
$EspressifConfigDir = "C:\Espressif\tools"
$SourceConfig = Join-Path $EspressifConfigDir "eim_idf.json"
$LocalConfig = Join-Path $EimStateDir "eim_idf.json"

Set-Location $ProjectRoot
New-Item -ItemType Directory -Path $EimStateDir -Force | Out-Null

if (-not (Get-Command eim -ErrorAction SilentlyContinue)) {
    throw "EIM не найден в PATH."
}

if (Test-Path $SourceConfig) {
    Copy-Item $SourceConfig $LocalConfig -Force
}

if (-not (Test-Path $LocalConfig)) {
    throw "Не найден eim_idf.json ни в $EspressifConfigDir, ни в $EimStateDir"
}

$env:PYTHONDONTWRITEBYTECODE = "1"

function Invoke-EimIdf {
    param(
        [Parameter(Mandatory=$true)]
        [string]$IdfCommand
    )

    Write-Host "ESP32: $IdfCommand"

    & eim `
        --log-file $EimLog `
        --esp-idf-json-path $EimStateDir `
        run $IdfCommand $IdfVersion

    $exitCode = $LASTEXITCODE

    if ($null -eq $exitCode) {
        $exitCode = 1
    }

    if ($exitCode -ne 0) {
        Write-Error "ESP32 BUILD FAILED (exit code $exitCode)"
        exit $exitCode
    }
}

switch ($Action) {
    "build" {
        Invoke-EimIdf "idf.py build"
    }
    "fullclean" {
        Invoke-EimIdf "idf.py fullclean"
    }
    "flash" {
        Invoke-EimIdf "idf.py -p $Port flash"
    }
    "monitor" {
        Invoke-EimIdf "idf.py -p $Port monitor"
    }
    "flash-monitor" {
        Invoke-EimIdf "idf.py -p $Port flash monitor"
    }
}

Write-Host "ESP32 BUILD SUCCESSFUL"
exit 0
