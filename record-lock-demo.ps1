[CmdletBinding()]
param(
    [ValidateSet('smoke', 'full')][string]$Mode = 'smoke',
    [string]$Serial = '192.168.1.65:33719',
    [ValidateRange(-1, 3)][int]$PowerLevel = -1,
    [string]$Adb = 'C:\Dev\AndroidSdk\platform-tools\adb.exe'
)
$ErrorActionPreference = 'Stop'
$demoArgs = @((Join-Path $PSScriptRoot 'tools\record-lock-demo.py'), '--mode', $Mode, '--serial', $Serial, '--adb', $Adb)
if ($PowerLevel -ge 0) { $demoArgs += @('--power-level', "$PowerLevel") }
$env:PYTHONUTF8 = '1'
& python @demoArgs
if ($LASTEXITCODE -ne 0) { throw 'Сценарий остановлен; смотрите журнал в out/demo-video.' }
