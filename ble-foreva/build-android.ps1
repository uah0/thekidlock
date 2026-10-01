[CmdletBinding()]
param([ValidateSet('build','lint')][string]$Action = 'build')
$ErrorActionPreference = 'Stop'
$ProjectRoot = $PSScriptRoot
$ProjectDir = Join-Path $ProjectRoot 'android'
$JavaHome = 'C:\Program Files\Android\Android Studio\jbr'
$AndroidSdk = Join-Path $env:LOCALAPPDATA 'Android\Sdk'
if (-not (Test-Path $AndroidSdk)) { $AndroidSdk = 'C:\Dev\AndroidSdk' }
if (-not (Test-Path (Join-Path $JavaHome 'bin\java.exe'))) { throw "JBR not found: $JavaHome" }
if (-not (Test-Path $AndroidSdk)) { throw "Android SDK not found: $AndroidSdk" }
$env:JAVA_HOME = $JavaHome
$env:ANDROID_HOME = $AndroidSdk
$env:ANDROID_SDK_ROOT = $AndroidSdk
$env:GRADLE_USER_HOME = Join-Path $ProjectRoot '.gradle-codex'
$env:ANDROID_USER_HOME = Join-Path $ProjectRoot '.android'
New-Item -ItemType Directory -Force $env:GRADLE_USER_HOME,$env:ANDROID_USER_HOME | Out-Null
$env:GRADLE_OPTS = "-Duser.home=`"$ProjectRoot`""
$env:Path = "$JavaHome\bin;$AndroidSdk\platform-tools;$env:Path"
Set-Location $ProjectDir
if ($Action -eq 'lint') { & .\gradlew.bat --no-daemon lintDebug }
else { & .\gradlew.bat --no-daemon assembleDebug }
exit $LASTEXITCODE
