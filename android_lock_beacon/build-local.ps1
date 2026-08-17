$ErrorActionPreference = "Stop"

$jdkPath = "C:\jdk17"
$gradleBat = Get-ChildItem `
    "C:\Users\User\.gradle\wrapper\dists\gradle-9.0.0-bin" `
    -Filter gradle.bat -Recurse -ErrorAction Stop |
    Select-Object -First 1 -ExpandProperty FullName

if (-not (Test-Path "$jdkPath\bin\java.exe")) {
    throw "JDK 17 not found at $jdkPath"
}
if (-not $gradleBat) {
    throw "Gradle 9 distribution not found in the user wrapper cache"
}

$env:JAVA_HOME = $jdkPath
$env:Path = "$jdkPath\bin;$env:Path"
$env:GRADLE_USER_HOME = "$PSScriptRoot\.gradle-project"
$env:GRADLE_RO_DEP_CACHE = "C:\Users\User\.gradle\caches"

& $gradleBat --offline --project-dir $PSScriptRoot assembleDebug
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "APK: $PSScriptRoot\app\build\outputs\apk\debug\app-debug.apk"
