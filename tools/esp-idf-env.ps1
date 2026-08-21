# Project-local ESP-IDF PowerShell environment.
# Load from a PowerShell profile with:
# . F:\ble-lock-project\tools\esp-idf-env.ps1

$script:EspIdfPath = 'C:\Espressif\frameworks\esp-idf-v5.4.4'
$script:EspIdfToolsPath = 'C:\Espressif'

$script:EspIdfPython = $null
$pythonCandidates = @(
    'C:\Espressif\python_env\idf5.4_py3.13_env\Scripts\python.exe',
    'C:\Espressif\python_env\idf5.4_py3.11_env\Scripts\python.exe'
)
foreach ($candidate in $pythonCandidates) {
    if (Test-Path -LiteralPath $candidate) {
        $script:EspIdfPython = $candidate
        break
    }
}

if (-not $script:EspIdfPython) {
    throw 'ESP-IDF Python environment was not found under C:\Espressif\python_env.'
}

$script:EspIdfPythonEnv = Split-Path -Parent (Split-Path -Parent $script:EspIdfPython)

$toolchainBins = @()
$xtensaCompiler = Get-ChildItem 'C:\Espressif\tools\xtensa-esp-elf' -Recurse `
    -Filter 'xtensa-esp32s3-elf-gcc.exe' -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending |
    Select-Object -First 1
if ($xtensaCompiler) {
    $toolchainBins += $xtensaCompiler.DirectoryName
}

$riscvCompiler = Get-ChildItem 'C:\Espressif\tools\riscv32-esp-elf' -Recurse `
    -Filter 'riscv32-esp-elf-gcc.exe' -ErrorAction SilentlyContinue |
    Select-Object -First 1
if ($riscvCompiler) {
    $toolchainBins += $riscvCompiler.DirectoryName
}

if ($toolchainBins.Count -eq 0) {
    throw 'ESP-IDF Xtensa toolchain was not found under C:\Espressif\tools.'
}

$xtensaBin = $toolchainBins[0]
$xtensaGxx = Join-Path $xtensaBin 'xtensa-esp32s3-elf-g++.exe'
if (-not (Test-Path -LiteralPath $xtensaGxx)) {
    throw "Xtensa C++ compiler was not found: $xtensaGxx"
}
$env:CC = Join-Path $xtensaBin 'xtensa-esp32s3-elf-gcc.exe'
$env:CXX = $xtensaGxx
$env:ASM = $env:CC

$env:IDF_PATH = $script:EspIdfPath
$env:IDF_TOOLS_PATH = $script:EspIdfToolsPath
$env:IDF_PYTHON_ENV_PATH = $script:EspIdfPythonEnv
$env:ESPTOOL_WRAPPER = Join-Path $PSScriptRoot 'esptool-wrapper.py'

$pathEntries = @((Split-Path -Parent $script:EspIdfPython)) +
    $toolchainBins +
    @(
        'C:\Espressif\tools\cmake\3.30.2\bin',
        'C:\Espressif\tools\ninja\1.12.1',
        "$script:EspIdfPath\components\esptool_py\esptool",
        $env:Path
    )
$env:Path = $pathEntries -join ';'

function global:idf.py {
    & $script:EspIdfPython "$script:EspIdfPath\tools\idf.py" @args
}

Write-Host "ESP-IDF готов: $script:EspIdfPath" -ForegroundColor Green
Write-Host 'Сборка: idf.py build' -ForegroundColor Green
