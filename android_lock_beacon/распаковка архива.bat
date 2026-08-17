@echo off
setlocal

set "WORKDIR=%~dp0"

"C:\Program Files\PowerShell\7\pwsh.exe" -NoProfile -Command ^
  "$zip = Get-ChildItem -LiteralPath '%WORKDIR%' -Filter '*.zip' -File | Sort-Object LastWriteTime -Descending | Select-Object -First 1; ^
   if ($null -eq $zip) { Write-Host 'ZIP-архивы не найдены'; exit 1 }; ^
   Write-Host ('Распаковка: ' + $zip.Name); ^
   Expand-Archive -LiteralPath $zip.FullName -DestinationPath '%WORKDIR%' -Force"

if errorlevel 1 pause