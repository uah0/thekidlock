[CmdletBinding()]
param(
    [ValidateSet("debug","release","clean","install","test")]
    [string]$Action = "debug",
    [ValidateSet("android_lock_beacon")]
    [string]$ProjectName = "android_lock_beacon",
    [string]$OtaFirmwareUrl = ""
)

$ErrorActionPreference = "Stop"

$ProjectRoot = $PSScriptRoot
$ProjectDir = Join-Path $ProjectRoot $ProjectName
$JavaHome = "C:\Program Files\Android\Android Studio\jbr"
$AndroidSdk = "C:\Dev\AndroidSdk"

$GradleHome = Join-Path $ProjectRoot ".gradle-codex"
$AndroidUserHome = Join-Path $ProjectRoot ".android"

if (-not (Test-Path (Join-Path $JavaHome "bin\java.exe"))) {
    throw "JBR Android Studio не найден: $JavaHome"
}
if (-not (Test-Path $AndroidSdk)) {
    throw "Android SDK не найден: $AndroidSdk"
}
if (-not (Test-Path $ProjectDir)) {
    throw "Android-проект не найден: $ProjectDir"
}

New-Item -ItemType Directory -Path $GradleHome -Force | Out-Null
New-Item -ItemType Directory -Path $AndroidUserHome -Force | Out-Null

$env:JAVA_HOME = $JavaHome
$env:GRADLE_USER_HOME = $GradleHome
$env:ANDROID_HOME = $AndroidSdk
$env:ANDROID_SDK_ROOT = $AndroidSdk
$env:ANDROID_USER_HOME = $AndroidUserHome
Remove-Item Env:ANDROID_PREFS_ROOT -ErrorAction SilentlyContinue

$env:Path = "$JavaHome\bin;$AndroidSdk\platform-tools;$env:Path"

$userHomeOption = "-Duser.home=`"$ProjectRoot`""
if ([string]::IsNullOrWhiteSpace($env:GRADLE_OPTS)) {
    $env:GRADLE_OPTS = $userHomeOption
} else {
    $env:GRADLE_OPTS = "$($env:GRADLE_OPTS) $userHomeOption"
}

$localProperties = Join-Path $ProjectDir "local.properties"
$sdkEscaped = $AndroidSdk.Replace('\', '\\').Replace(':', '\:')
Set-Content -Path $localProperties -Value "sdk.dir=$sdkEscaped" -Encoding ASCII

Set-Location $ProjectDir

$gradleProperties = @("--no-daemon")
if (-not [string]::IsNullOrWhiteSpace($OtaFirmwareUrl)) {
    $parsedOtaUrl = [Uri]$OtaFirmwareUrl
    $isLocalTestUrl = $parsedOtaUrl.Scheme -eq "http" -and
        $parsedOtaUrl.Host -in @("127.0.0.1", "localhost")
    $pinnedReleaseUrl = "https://github.com/uah0/thekidlock/releases/latest/download/ble_lock_stage1_s3.bin"
    if ($OtaFirmwareUrl -ne $pinnedReleaseUrl -and -not $isLocalTestUrl) {
        throw "OtaFirmwareUrl должен совпадать с закреплённым GitHub asset или использовать HTTP только для localhost."
    }
    $gradleProperties += "-PotaFirmwareUrl=$OtaFirmwareUrl"
}

if ($Action -in @("debug", "release", "install")) {
    $buildNumberFile = Join-Path $ProjectDir "BUILD_NUMBER"
    $buildNumber = [int](Get-Content -LiteralPath $buildNumberFile -Raw).Trim()
    Set-Content -LiteralPath $buildNumberFile -Value ($buildNumber + 1) -Encoding ASCII
}

switch ($Action) {
    "release" {
        if (-not (Test-Path (Join-Path $ProjectDir "keystore.properties"))) {
            throw "Нет постоянной подписи. Выполните tools/prepare_android_signing.py."
        }
        & ".\gradlew.bat" @gradleProperties assembleRelease
    }
    "test" {
        & ".\gradlew.bat" @gradleProperties test
    }
    "debug" {
        & ".\gradlew.bat" @gradleProperties assembleDebug
    }
    "clean" {
        & ".\gradlew.bat" --no-daemon clean
    }
    "install" {
        & ".\gradlew.bat" @gradleProperties assembleDebug
        if ($LASTEXITCODE -eq 0) {
            $apk = Join-Path $ProjectDir "app\build\outputs\apk\debug\app-debug.apk"
            if (-not (Test-Path $apk)) {
                throw "APK не найден: $apk"
            }
            & adb install -r $apk
        }
    }
}

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
