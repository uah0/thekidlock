[CmdletBinding()]
param([string]$NotesFile = "")
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
$arguments = @("tools/publish_android_release.py")
if ($NotesFile) { $arguments += @("--notes-file", $NotesFile) }
& "C:\Espressif\tools\python\python.exe" @arguments
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
