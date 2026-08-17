@echo off
pwsh.exe -Command "Set-Location -LiteralPath '%~dp0'; ./gradlew assembleDebug && adb install -r app/build/outputs/apk/debug/app-debug.apk"
if errorlevel 1 pause