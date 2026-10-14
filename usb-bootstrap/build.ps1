[CmdletBinding()]
param(
    [string]$BuildDir = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $projectRoot 'build-signed-dev' }
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path

$config = Get-Content -LiteralPath (Join-Path $BuildDir 'sdkconfig') -Raw
foreach ($required in @(
    'CONFIG_IDF_TARGET="esp32s3"',
    'CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y',
    'CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES=y',
    '# CONFIG_SECURE_BOOT is not set',
    '# CONFIG_SECURE_FLASH_ENC_ENABLED is not set',
    '# CONFIG_FLASH_ENCRYPTION_ENABLED is not set',
    '# CONFIG_NVS_ENCRYPTION is not set'
)) {
    if ($config -notmatch "(?m)^$([regex]::Escape($required))\r?$") {
        throw "Сборка не соответствует USB-профилю нового ESP32-S3: $required"
    }
}

$number = (Get-Content -LiteralPath (Join-Path $projectRoot 'BUILD_NUMBER') -Raw).Trim()
if ($number -notmatch '^\d+$') { throw 'BUILD_NUMBER содержит некорректное значение.' }
$header = Get-Content -LiteralPath (Join-Path $BuildDir 'generated/app_build_number.h') -Raw
if ($header -notmatch '(?m)^#define APP_BUILD_NUMBER_STRING "(\d+)"\r?$' -or $Matches[1] -ne $number) {
    throw 'Номер приложения в build-signed-dev не совпадает с BUILD_NUMBER; сначала соберите ESP.'
}

$parts = @(
    [pscustomobject]@{ Offset = 0x0;     File = 'bootloader/bootloader.bin' },
    [pscustomobject]@{ Offset = 0x8000;  File = 'partition_table/partition-table.bin' },
    [pscustomobject]@{ Offset = 0xF000;  File = 'ota_data_initial.bin' },
    [pscustomobject]@{ Offset = 0x20000; File = 'ble_lock_stage1_s3.bin' }
)
$components = @()
$end = 0
foreach ($part in $parts) {
    $path = Join-Path $BuildDir $part.File
    $bytes = [IO.File]::ReadAllBytes($path)
    if ($part.Offset -lt $end) { throw "Перекрываются компоненты USB-образа: $($part.File)" }
    $end = $part.Offset + $bytes.Length
    if ($end -gt 0x400000) { throw 'Образ не помещается во флеш 4 MB.' }
    if ($part.File -eq 'ble_lock_stage1_s3.bin' -and $bytes.Length -gt 0x1E0000) {
        throw 'Приложение не помещается в раздел ota_0.'
    }
    $components += [pscustomobject]@{
        address = ('0x{0:X}' -f $part.Offset)
        file = $part.File
        size_bytes = $bytes.Length
        sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        bytes = $bytes
        offset = $part.Offset
    }
}

$image = [byte[]]::new($end)
[Array]::Fill[byte]($image, [byte]255)
foreach ($part in $components) {
    [Buffer]::BlockCopy($part.bytes, 0, $image, $part.offset, $part.bytes.Length)
}
for ($address = 0x9000; $address -lt 0xF000; $address++) {
    if ($image[$address] -ne 255) { throw 'Диапазон NVS в USB-образе должен оставаться пустым.' }
}

$output = Join-Path $PSScriptRoot 'thekidlock-usb-bootstrap.bin'
$temporary = "$output.tmp"
try {
    [IO.File]::WriteAllBytes($temporary, $image)
    [IO.File]::Move($temporary, $output, $true)
} finally {
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary }
}
$hash = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash.ToLowerInvariant()
$manifest = [ordered]@{
    image = 'thekidlock-usb-bootstrap.bin'
    firmware_version = "10.0.$number"
    profile = 'signed-dev'
    target = 'new, unprotected ESP32-S3 with 4 MB flash'
    flash_address = '0x0'
    size_bytes = $image.Length
    sha256 = $hash
    nvs_erased = $true
    efuse_changes = $false
    components = @($components | ForEach-Object {
        [ordered]@{ address = $_.address; file = $_.file; size_bytes = $_.size_bytes; sha256 = $_.sha256 }
    })
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'manifest.json') -Encoding utf8
"$hash  thekidlock-usb-bootstrap.bin" | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'thekidlock-usb-bootstrap.sha256') -Encoding utf8
Write-Host "USB bootstrap 10.0.$number готов: $output"
Write-Host "SHA-256: $hash"
