[CmdletBinding()]
param(
    [ValidateSet('build','flash','monitor')][string]$Action = 'build',
    [string]$Port = ''
)
$ErrorActionPreference = 'Stop'
$ProjectRoot = $PSScriptRoot
$ProjectDir = Join-Path $ProjectRoot 'esp32'
$EimStateDir = Join-Path $ProjectRoot '.eim-codex'
New-Item -ItemType Directory -Force $EimStateDir | Out-Null
$SourceConfig = 'C:\Espressif\tools\eim_idf.json'
$LocalConfig = Join-Path $EimStateDir 'eim_idf.json'
if (Test-Path $SourceConfig) { Copy-Item $SourceConfig $LocalConfig -Force }
if (-not (Test-Path $LocalConfig)) { throw 'EIM config not found' }
Set-Location $ProjectDir
$command = switch ($Action) {
    'build' { 'idf.py build' }
    'flash' { if (-not $Port) { throw 'Use -Port COMx for the dedicated scanner board' }; "idf.py -p $Port flash" }
    'monitor' { if (-not $Port) { throw 'Use -Port COMx for the dedicated scanner board' }; "idf.py -p $Port monitor" }
}
& eim --log-file (Join-Path $EimStateDir 'eim.log') --esp-idf-json-path $EimStateDir run $command v5.4.4
exit $LASTEXITCODE
