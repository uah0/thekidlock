[CmdletBinding()]
param([string]$Serial = '')
$ErrorActionPreference = 'Stop'
$ProjectRoot = $PSScriptRoot
$AndroidSdk = Join-Path $env:LOCALAPPDATA 'Android\Sdk'
if (-not (Test-Path $AndroidSdk)) { $AndroidSdk = 'C:\Dev\AndroidSdk' }
$Adb = Join-Path $AndroidSdk 'platform-tools\adb.exe'
$Apk = Join-Path $ProjectRoot 'android\app\build\outputs\apk\debug\app-debug.apk'
if (-not (Test-Path $Adb)) { throw "adb not found: $Adb" }
if (-not (Test-Path $Apk)) { throw "APK not found: $Apk" }
$DeviceArgs = @()
if ($Serial) { $DeviceArgs = @('-s', $Serial) }
function Invoke-Adb([string[]]$AdbArgs) {
    $output = & $Adb @DeviceArgs @AdbArgs
    if ($LASTEXITCODE -ne 0) { throw "adb failed: $($AdbArgs -join ' ')" }
    return ($output | Out-String).Trim()
}
$before = [ordered]@{
    serial = $Serial
    timestamp = (Get-Date).ToString('o')
    accelerometer_rotation = Invoke-Adb @('shell','settings','get','system','accelerometer_rotation')
    user_rotation = Invoke-Adb @('shell','settings','get','system','user_rotation')
}
$ArtifactDir = Join-Path $ProjectRoot 'artifacts'
New-Item -ItemType Directory -Force $ArtifactDir | Out-Null
$BeforePath = Join-Path $ArtifactDir ("android-rotation-before-" + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.json')
$before | ConvertTo-Json | Set-Content -LiteralPath $BeforePath -Encoding UTF8
Write-Host "Saved orientation values: $BeforePath"
try {
    Write-Host (Invoke-Adb @('install','-r',$Apk))
} finally {
    if ($before.accelerometer_rotation -eq 'null') {
        Invoke-Adb @('shell','settings','delete','system','accelerometer_rotation') | Out-Null
    } else {
        Invoke-Adb @('shell','settings','put','system','accelerometer_rotation',[string]$before.accelerometer_rotation) | Out-Null
    }
    if ($before.user_rotation -eq 'null') {
        Invoke-Adb @('shell','settings','delete','system','user_rotation') | Out-Null
    } else {
        Invoke-Adb @('shell','settings','put','system','user_rotation',[string]$before.user_rotation) | Out-Null
    }
    $afterAccelerometer = Invoke-Adb @('shell','settings','get','system','accelerometer_rotation')
    $afterUser = Invoke-Adb @('shell','settings','get','system','user_rotation')
    if ($afterAccelerometer -ne $before.accelerometer_rotation -or $afterUser -ne $before.user_rotation) {
        throw "Orientation restoration verification failed: accelerometer=$afterAccelerometer user=$afterUser"
    }
    Write-Host "Orientation restored and verified: accelerometer_rotation=$afterAccelerometer user_rotation=$afterUser"
}
