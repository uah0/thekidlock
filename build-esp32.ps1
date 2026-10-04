[CmdletBinding()]
param(
    [ValidateSet("build","fullclean","flash","monitor","flash-monitor")]
    [string]$Action = "build",
    [string]$IdfVersion = "v5.4.4",
    [string]$Port = ""
)

$ErrorActionPreference = "Stop"

$ProjectRoot = $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot "build"
$EimStateDir = Join-Path $ProjectRoot ".eim-codex"
$EimLog = Join-Path $EimStateDir "eim.log"
$SourceConfig = "C:\Espressif\tools\eim_idf.json"
$LocalConfig = Join-Path $EimStateDir "eim_idf.json"
$IdfPython = "C:\Espressif\tools\python\v5.4.4\venv\Scripts\python.exe"

Set-Location $ProjectRoot
New-Item -ItemType Directory -Path $EimStateDir -Force | Out-Null

if (-not (Get-Command eim -ErrorAction SilentlyContinue)) {
    throw "EIM не найден в PATH."
}
if (-not (Test-Path $IdfPython)) {
    throw "Python ESP-IDF не найден: $IdfPython"
}
if (Test-Path $SourceConfig) {
    Copy-Item $SourceConfig $LocalConfig -Force
}
if (-not (Test-Path $LocalConfig)) {
    throw "Не найден eim_idf.json"
}

$env:PYTHONDONTWRITEBYTECODE = "1"

function Invoke-EimIdf {
    param([Parameter(Mandatory=$true)][string]$IdfCommand)

    & eim `
        --log-file $EimLog `
        --esp-idf-json-path $EimStateDir `
        run $IdfCommand $IdfVersion

    $code = $LASTEXITCODE
    if ($null -eq $code) { $code = 1 }
    if ($code -ne 0) {
        Write-Error "ESP-IDF command failed (exit code $code)"
        exit $code
    }
}

function Apply-NimbleSecurityGuard {
    Invoke-EimIdf "python tools/apply_nimble_sec_req_guard.py"
}

function Resolve-EspPort {
    if (-not [string]::IsNullOrWhiteSpace($Port)) {
        return $Port.ToUpperInvariant()
    }

    $ports = @(
        Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue |
        Where-Object {
            $_.PNPDeviceID -like "USB\VID_303A&PID_1001*" -or
            $_.Name -match "ESP32|USB Serial"
        } |
        Select-Object -ExpandProperty DeviceID -Unique
    )

    if ($ports.Count -eq 0) {
        throw "Не найден активный COM-порт ESP32-S3. Укажите -Port COMx."
    }
    if ($ports.Count -gt 1) {
        throw "Найдено несколько COM-портов: $($ports -join ', '). Укажите -Port COMx."
    }

    return $ports[0]
}

function Invoke-DirectFlash {
    $resolvedPort = Resolve-EspPort
    $flashArgs = Join-Path $BuildDir "flash_args"

    if (-not (Test-Path $flashArgs)) {
        Apply-NimbleSecurityGuard
        Invoke-EimIdf "idf.py build"
    }
    if (-not (Test-Path $flashArgs)) {
        throw "Не найден $flashArgs"
    }

    Write-Host "Flashing ESP32-S3 on $resolvedPort via direct esptool..."

    Push-Location $BuildDir
    try {
        & $IdfPython `
            -m esptool `
            --chip esp32s3 `
            --port $resolvedPort `
            -b 460800 `
            --before default_reset `
            --after hard_reset `
            write_flash `
            "@flash_args"

        $code = $LASTEXITCODE
        if ($null -eq $code) { $code = 1 }
        if ($code -ne 0) {
            Write-Error "ESP32 FLASH FAILED (exit code $code)"
            exit $code
        }
    }
    finally {
        Pop-Location
    }

    Write-Host "ESP32 FLASH SUCCESSFUL"
}

switch ($Action) {
    "build" {
        Apply-NimbleSecurityGuard
        Invoke-EimIdf "idf.py build"
        Write-Host "ESP32 BUILD SUCCESSFUL"
    }
    "fullclean" {
        Invoke-EimIdf "idf.py fullclean"
        Write-Host "ESP32 FULLCLEAN SUCCESSFUL"
    }
    "flash" {
        Invoke-DirectFlash
    }
    "monitor" {
        $p = Resolve-EspPort
        Invoke-EimIdf "idf.py -p $p monitor"
    }
    "flash-monitor" {
        Invoke-DirectFlash
        $p = Resolve-EspPort
        Invoke-EimIdf "idf.py -p $p monitor"
    }
}

exit 0
