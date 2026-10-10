[CmdletBinding()]
param(
    [ValidateSet("build","fullclean","flash","monitor","flash-monitor")]
    [string]$Action = "build",
    [ValidateSet("default","signed-dev","factory-test","release","finalize-existing")]
    [string]$Profile = "signed-dev",
    [string]$IdfVersion = "v5.4.4",
    [string]$Port = ""
)

$ErrorActionPreference = "Stop"

if ($Profile -in @("factory-test", "release", "finalize-existing") -and $Action -in @("flash", "flash-monitor")) {
    throw "Фабричный профиль изменяет eFuse при первом запуске. Обычная прошивка запрещена; требуется отдельный согласованный фабричный этап."
}

$ProjectRoot = $PSScriptRoot
$BuildDir = if ($Profile -eq "default") {
    Join-Path $ProjectRoot "build"
} else {
    Join-Path $ProjectRoot "build-$Profile"
}
$SigningKey = Join-Path $ProjectRoot ".security\ota_signing_key.pem"
$Sdkconfig = if ($Profile -eq "default") { Join-Path $ProjectRoot "sdkconfig" } else { Join-Path $BuildDir "sdkconfig" }
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
$env:PYTHONIOENCODING = "utf-8"

function Invoke-EimIdf {
    param([Parameter(Mandatory=$true)][string]$IdfCommand)

    $encodedCommand = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($IdfCommand))
    $childCodes = [System.Collections.Generic.List[int]]::new()
    & eim `
        --log-file $EimLog `
        --esp-idf-json-path $EimStateDir `
        run "python tools/invoke_idf_checked.py $encodedCommand" $IdfVersion |
        ForEach-Object {
            $line = [string]$_
            if ($line -match '^CODEX_IDF_EXIT_CODE=(-?\d+)\s*$') {
                $childCodes.Add([int]$Matches[1])
            }
            Write-Host $line
        }

    $code = $LASTEXITCODE
    if ($null -eq $code) { $code = 1 }
    if ($code -ne 0) {
        Write-Error "ESP-IDF command failed (exit code $code)"
        exit $code
    }
    if ($childCodes.Count -ne 1 -or $childCodes[0] -ne 0) {
        throw "ESP-IDF child command failed or did not report its exit status: $IdfCommand"
    }
}

function Ensure-MqttBuildDependency {
    $dependencyDir = Join-Path $EimStateDir "python-deps"
    & $IdfPython -c "import sys; sys.path.insert(0, sys.argv[1]); import paho.mqtt; assert paho.mqtt.__version__ == '2.1.0'" $dependencyDir 2>$null
    if ($LASTEXITCODE -ne 0) {
        & $IdfPython -m pip install --disable-pip-version-check --upgrade --target $dependencyDir -r (Join-Path $ProjectRoot "tools/requirements-build.txt")
        if ($LASTEXITCODE -ne 0) { throw "Не удалось установить зависимость проверки MQTT-брокеров" }
    }
}

function Apply-NimbleSecurityGuard {
    Invoke-EimIdf "python tools/apply_nimble_sec_req_guard.py"
}

function Invoke-ProjectIdf {
    param([Parameter(Mandatory=$true)][string]$IdfAction)
    if ($Profile -eq "default") {
        Invoke-EimIdf "idf.py $IdfAction"
    } else {
        if (-not (Test-Path $SigningKey)) {
        throw "Для профиля '$Profile' нужен закрытый ключ $SigningKey. Создайте его командой: idf.py secure-generate-signing-key $SigningKey"
        }
        $defaults = "$ProjectRoot/sdkconfig.defaults;$ProjectRoot/sdkconfig.defaults.$Profile"
        if ($Profile -eq "factory-test") {
            $defaults = "$ProjectRoot/sdkconfig.defaults;$ProjectRoot/sdkconfig.defaults.release;$ProjectRoot/sdkconfig.defaults.factory-test"
        }
        $command = "idf.py -B `"$BuildDir`" -D `"SDKCONFIG=$Sdkconfig`" -D `"SDKCONFIG_DEFAULTS=$defaults`" $IdfAction"
        Invoke-EimIdf $command
    }
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
            Invoke-ProjectIdf "build"
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
        Ensure-MqttBuildDependency
        Apply-NimbleSecurityGuard
        Invoke-ProjectIdf "build"
        if ($Profile -ne "default") {
            Invoke-EimIdf "python tools/prepare_encrypted_ota.py `"$BuildDir`""
        }
        Write-Host "ESP32 BUILD SUCCESSFUL"
    }
    "fullclean" {
        Invoke-ProjectIdf "fullclean"
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
