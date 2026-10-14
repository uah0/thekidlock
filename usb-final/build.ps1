[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root 'build-factory-usb'
$sdkconfig = Join-Path $buildDir 'sdkconfig'
$imagePath = Join-Path $PSScriptRoot 'thekidlock-usb-final.bin'

& (Join-Path $root 'build-esp32.ps1') -Profile factory-usb
if ($LASTEXITCODE -ne 0) { throw 'Сборка защищённого профиля завершилась ошибкой.' }

$config = Get-Content -LiteralPath $sdkconfig -Raw
foreach ($required in @(
    'CONFIG_IDF_TARGET="esp32s3"',
    'CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y',
    'CONFIG_SECURE_BOOT=y',
    'CONFIG_SECURE_FLASH_ENC_ENABLED=y',
    'CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE=y',
    'CONFIG_SECURE_DISABLE_ROM_DL_MODE=y',
    'CONFIG_NVS_ENCRYPTION=y',
    'CONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC=y',
    'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_factory_usb.csv"'
)) {
    if ($config -notmatch "(?m)^$([regex]::Escape($required))\r?$") {
        throw "Профиль не соответствует второму USB-этапу: $required"
    }
}

$parts = @(
    [pscustomobject]@{ Offset = 0x0;     File = 'bootloader/bootloader.bin' },
    [pscustomobject]@{ Offset = 0x8000;  File = 'partition_table/partition-table.bin' },
    [pscustomobject]@{ Offset = 0xF000;  File = 'ota_data_initial.bin' },
    [pscustomobject]@{ Offset = 0x20000; File = 'ble_lock_stage1_s3.bin' }
)
$image = [byte[]]::new(0x400000)
[Array]::Fill[byte]($image, [byte]255)
$components = @()
foreach ($part in $parts) {
    $path = Join-Path $buildDir $part.File
    $bytes = [IO.File]::ReadAllBytes($path)
    if ($part.Offset + $bytes.Length -gt $image.Length) { throw "Компонент выходит за пределы 4 MB: $($part.File)" }
    [Buffer]::BlockCopy($bytes, 0, $image, $part.Offset, $bytes.Length)
    $components += [pscustomobject]@{
        address = ('0x{0:X}' -f $part.Offset)
        file = $part.File
        size_bytes = $bytes.Length
        sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

$python = 'C:\Espressif\tools\python\v5.4.4\venv\Scripts\python.exe'
$mergePath = "$imagePath.merge"
$mergeArgs = @('-m','esptool','--chip','esp32s3','merge_bin','--flash_mode','dio','--flash_size','4MB','-o',$mergePath)
foreach ($part in $parts) { $mergeArgs += @(('0x{0:X}' -f $part.Offset),(Join-Path $buildDir $part.File)) }
& $python @mergeArgs
if ($LASTEXITCODE -ne 0) { throw 'esptool merge_bin завершился ошибкой.' }
$reference = [IO.File]::ReadAllBytes($mergePath)
if ($reference.Length -gt $image.Length) { throw 'esptool создал образ больше 4 MB.' }
$mergedImage = [byte[]]::new(0x400000)
[Array]::Fill[byte]($mergedImage, [byte]255)
[Buffer]::BlockCopy($reference, 0, $mergedImage, 0, $reference.Length)
if (-not [Linq.Enumerable]::SequenceEqual[byte]($image, $mergedImage)) { throw 'Независимый esptool merge_bin не совпал с образом.' }
Remove-Item -LiteralPath $mergePath

foreach ($range in @(@(0x9000,0xEFFF),@(0x12000,0x12FFF))) {
    for ($i = $range[0]; $i -le $range[1]; $i++) {
        if ($image[$i] -ne 255) { throw ('Раздел по адресу 0x{0:X} должен быть пустым.' -f $range[0]) }
    }
}
[IO.File]::WriteAllBytes($imagePath, $image)
$hash = (Get-FileHash -LiteralPath $imagePath -Algorithm SHA256).Hash.ToLowerInvariant()
$number = (Get-Content -LiteralPath (Join-Path $root 'BUILD_NUMBER') -Raw).Trim()
$manifest = [ordered]@{
    image = 'thekidlock-usb-final.bin'
    firmware_version = "10.0.$number"
    profile = 'factory-usb'
    target = 'new ESP32-S3 with 4 MB flash; per-chip keys are generated on first boot'
    flash_address = '0x0'
    size_bytes = $image.Length
    sha256 = $hash
    nvs_empty = $true
    nvs_keys_empty = $true
    per_device_keys_generated_on_first_boot = $true
    secure_boot_and_flash_encryption_release = $true
    rom_download_disabled_on_first_boot = $true
    efuse_changes_on_first_boot = $true
    components = $components
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'manifest.json') -Encoding utf8
Write-Host "Финальный USB-образ 10.0.$number создан: $imagePath"
Write-Host "SHA-256: $hash"
Write-Warning 'Первый запуск этого образа необратимо изменит eFuse. Не записывайте его до отдельного разрешения на этот ESP.'
