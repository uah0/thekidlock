[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Port,
    [ValidateSet('STAT','RAW','CSV')][string]$Mode = 'STAT',
    [string]$OutputPath = ''
)
$ErrorActionPreference = 'Stop'
$ProjectRoot = $PSScriptRoot
if (-not $OutputPath) {
    $OutputPath = Join-Path $ProjectRoot ('artifacts\esp32-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')
}
$OutputPath = [System.IO.Path]::GetFullPath($OutputPath)
New-Item -ItemType Directory -Path ([System.IO.Path]::GetDirectoryName($OutputPath)) -Force | Out-Null
$IdfPython = 'C:\Espressif\tools\python\v5.4.4\venv\Scripts\python.exe'
if (-not (Test-Path $IdfPython)) { throw "ESP-IDF Python not found: $IdfPython" }
& $IdfPython (Join-Path $ProjectRoot 'capture_serial.py') --port $Port --mode $Mode --output $OutputPath
exit $LASTEXITCODE
