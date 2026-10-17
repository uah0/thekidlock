[CmdletBinding(PositionalBinding=$false)]
param(
    [Parameter(Position=0)]
    [ValidateSet('capabilities', 'command', 'discover', 'info', 'status', 'ulp', 'events', 'snapshot',
                 'watch', 'open', 'close', 'auto', 'always-open',
                 'toggle-hold-open', 'arm-open', 'disable-auto-close',
                 'ulp-lock', 'ulp-unlock', 'ulp-gate-test', 'ulp-gate-test-stop', 'buzzer', 'diagnostics-on',
                 'diagnostics-off', 'log', 'monitor', 'capture', 'restart', 'flash')]
    [string]$Action = 'snapshot',
    [Parameter(Position=1)]
    [string]$Operand = '',
    [string]$Device = '',
    [string]$CommandName = '',
    [hashtable]$Arguments = @{},
    [ValidateRange(1, 600)]
    [int]$RestartTimeoutSec = 60,
    [int]$IntervalMs = 500,
    [ValidateRange(1, 7)]
    [int]$BuzzerPattern = 1,
    [string]$Firmware = '',
    [string]$Output = ''
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = $PSScriptRoot
$StateDir = Join-Path $ProjectRoot '.device-agent'
$StateFile = Join-Path $StateDir 'connection.json'

function Normalize-DeviceAddress([string]$Address) {
    if ([string]::IsNullOrWhiteSpace($Address)) { return '' }
    $Address = $Address.TrimEnd('/')
    if ($Address -notmatch '^https?://') { $Address = "http://$Address" }
    return $Address
}

function Invoke-DeviceGet([string]$Base, [string]$Path) {
    Invoke-RestMethod -Uri "$Base$Path" -Method Get -TimeoutSec 3
}

function Test-Device([string]$Base) {
    try {
        $info = Invoke-DeviceGet $Base '/api/agent/info'
        if (($info.agent_api_version -eq 1 -or
             (!$info.agent_api_version -and $info.api_version -eq 1)) -and $info.device_type) {
            return $info
        }
    } catch {}
    return $null
}

function Get-FirmwareVersion($Info) {
    if ($Info.firmware_version) { return $Info.firmware_version }
    return $Info.firmware # Agent v1 before component extraction
}

function Get-AgentPath([string]$Operation, $Info) {
    if (!$Info.agent_api_version) {
        switch ($Operation) {
            'status' { return '/api/status' }
            'restart' { return '/api/system/restart' }
            'ota' { return '/api/ota' }
        }
    }
    return "/api/agent/$Operation"
}

function Invoke-AgentCommand([string]$Base, [string]$Name, [hashtable]$Values = @{}) {
    if ([string]::IsNullOrWhiteSpace($Name)) { throw 'Укажите command NAME или -CommandName NAME.' }
    if ($Values.Count -gt 8 -or $Values.ContainsKey('action')) { throw 'Допускается до 8 аргументов, кроме action.' }
    $query = 'action=' + [uri]::EscapeDataString($Name)
    foreach ($key in $Values.Keys) {
        $query += '&' + [uri]::EscapeDataString([string]$key) + '=' +
            [uri]::EscapeDataString([string]$Values[$key])
    }
    Invoke-RestMethod -Uri "$Base/api/agent/command?$query" -Method Post -TimeoutSec 3
}

function Wait-AgentAfterFlash([string]$Base, $Before, [int]$TimeoutSec) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $sawOffline = $false
    do {
        Start-Sleep -Seconds 2
        $after = Test-Device $Base
        if (!$after) { $sawOffline = $true; continue }
        if ($Before.device_id -and $after.device_id -ne $Before.device_id) {
            throw 'После OTA по этому адресу отвечает другое устройство (device_id).'
        }
        $rebooted = if ($Before.boot_id) {
            $after.boot_id -and $after.boot_id -ne $Before.boot_id
        } else {
            $sawOffline -or ($after.boot_id) -or
                (Get-FirmwareVersion $after) -ne (Get-FirmwareVersion $Before) -or
                $after.build -ne $Before.build
        }
        if ($rebooted) { return $after }
    } while ((Get-Date) -lt $deadline)
    if (!$after) { throw "Устройство не вернулось в сеть в течение $TimeoutSec секунд после OTA." }
    throw 'Agent API отвечает, но перезапуск после OTA не подтверждён.'
}

function Save-Device([string]$Base) {
    New-Item -ItemType Directory -Path $StateDir -Force | Out-Null
    @{ device = $Base; saved_at = (Get-Date).ToString('o') } |
        ConvertTo-Json | Set-Content -LiteralPath $StateFile -Encoding utf8
}

function Find-Device {
    $candidates = [System.Collections.Generic.HashSet[string]]::new()
    $arpOutput = & arp.exe -a 2>$null
    foreach ($line in $arpOutput) {
        if ($line -match '(?<![\d.])((?:\d{1,3}\.){3}\d{1,3})(?![\d.])') {
            [void]$candidates.Add("http://$($Matches[1])")
        }
    }
    foreach ($candidate in $candidates) {
        $info = Test-Device $candidate
        if ($info) {
            Save-Device $candidate
            return [pscustomobject]@{ device = $candidate; info = $info }
        }
    }
    throw 'ESP32 не найдена в ARP-кэше. Укажите -Device 192.168.x.x или сначала откройте её Web UI.'
}

function Resolve-Device {
    $base = Normalize-DeviceAddress $Device
    if (-not $base -and (Test-Path -LiteralPath $StateFile)) {
        $base = Normalize-DeviceAddress ((Get-Content -Raw -LiteralPath $StateFile | ConvertFrom-Json).device)
    }
    if (-not $base) { $base = (Find-Device).device }
    $info = Test-Device $base
    if (-not $info) { throw "Устройство не отвечает как Agent API v1: $base" }
    Save-Device $base
    return $base
}

if ($Action -eq 'discover') {
    Find-Device | ConvertTo-Json -Depth 8
    exit 0
}

$base = Resolve-Device
$deviceInfo = Test-Device $base
if (!$deviceInfo) { throw "Устройство перестало отвечать: $base" }
switch ($Action) {
    'info'   { Invoke-DeviceGet $base '/api/agent/info' | ConvertTo-Json -Depth 8 }
    'capabilities' { Invoke-DeviceGet $base '/api/agent/capabilities' | ConvertTo-Json -Depth 8 }
    'command' {
        if (!$CommandName) { $CommandName = $Operand }
        Invoke-AgentCommand $base $CommandName $Arguments | ConvertTo-Json -Depth 8
    }
    'status' { Invoke-DeviceGet $base (Get-AgentPath 'status' $deviceInfo) | ConvertTo-Json -Depth 8 }
    'ulp'    { Invoke-DeviceGet $base '/api/ulp' | ConvertTo-Json -Depth 8 }
    'events' { Invoke-DeviceGet $base '/api/agent/events' | ConvertTo-Json -Depth 8 }
    'snapshot' {
        $status = Invoke-DeviceGet $base (Get-AgentPath 'status' $deviceInfo)
        $snapshot = [ordered]@{
            device = $base
            captured_at = (Get-Date).ToString('o')
            info = $deviceInfo
            status = $status
        }
        if ($deviceInfo.agent_api_version) {
            $caps = Invoke-DeviceGet $base '/api/agent/capabilities'
            $snapshot.capabilities = $caps
            if ($caps.features.events) { $snapshot.events = Invoke-DeviceGet $base '/api/agent/events' }
            if ($status.ulp) { $snapshot.ulp = $status.ulp }
        } else {
            # Compatibility with the original BLE Lock Agent API.
            $snapshot.ulp = Invoke-DeviceGet $base '/api/ulp'
            $snapshot.events = Invoke-DeviceGet $base '/api/agent/events'
        }
        [pscustomobject]$snapshot | ConvertTo-Json -Depth 12
    }
    'watch' {
        while ($true) {
            $ulp = Invoke-DeviceGet $base '/api/ulp'
            [pscustomobject]@{
                time = (Get-Date).ToString('HH:mm:ss.fff')
                lock = $ulp.lock_state_name
                fsm = $ulp.fsm_state_name
                plate = $ulp.plate_state_name
                delta = $ulp.delta_raw
                verify = $ulp.close_verify_state
                verify_adjusted = $ulp.close_verify_adjusted_raw
                attempts = $ulp.close_attempt_counter
                rejected = $ulp.close_reject_latched
                overlap = $ulp.command_overlap_counter
                gpio9 = $ulp.gate_level
            } | ConvertTo-Json -Compress
            Start-Sleep -Milliseconds ([Math]::Max(100, $IntervalMs))
        }
    }
    'buzzer' { Invoke-AgentCommand $base 'buzzer' @{value=$BuzzerPattern} | ConvertTo-Json -Depth 8 }
    'diagnostics-on' { Invoke-AgentCommand $base 'diagnostics' @{value=1} | ConvertTo-Json -Depth 8 }
    'diagnostics-off' { Invoke-AgentCommand $base 'diagnostics' @{value=0} | ConvertTo-Json -Depth 8 }
    'log' {
        (Invoke-WebRequest -Uri "$base/api/agent/monitor?since=0" -TimeoutSec 8).Content
    }
    'monitor' {
        $cursor = [uint64]0
        while ($true) {
            $response = Invoke-WebRequest -Uri "$base/api/agent/monitor?since=$cursor" `
                -TimeoutSec 8
            $droppedHeader = @($response.Headers['X-Log-Dropped'])[0]
            if ($droppedHeader -eq '1') {
                Write-Warning 'Часть старого журнала вытеснена из RAM ESP32'
            }
            if ($response.Content) { Write-Host -NoNewline $response.Content }
            $cursor = [uint64](@($response.Headers['X-Log-Cursor'])[0])
            Start-Sleep -Milliseconds ([Math]::Max(100, $IntervalMs))
        }
    }
    'capture' {
        $path = if ($Output) { $Output } else {
            Join-Path (Get-Location) ("ulp-pulses-{0}.csv" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
        }
        $fullPath = [System.IO.Path]::GetFullPath($path)
        $utf8 = [System.Text.UTF8Encoding]::new($false)
        [System.IO.File]::WriteAllText($fullPath,
            "captured_at,pulse_sequence,measurement_counter,pulse_us,baseline_raw,vbat_raw,sense_raw,delta_raw,normalized_feature,plate_state,fault_flags,lock_state,fsm_state,event_flags,command,source_dropped_before`r`n", $utf8)
        Write-Host "Запись импульсов в $fullPath. Остановите Ctrl+C."
        $cursor = [uint64]0
        $lastSequence = [uint32]0
        $warnedRingLoss = $false
        $warnedSourceLoss = $false
        while ($true) {
            try {
                do {
                    $response = Invoke-WebRequest -Uri "$base/api/agent/pulses?since=$cursor" `
                        -TimeoutSec 8
                    if (@($response.Headers['X-Pulse-Dropped'])[0] -eq '1' -and !$warnedRingLoss) {
                        Write-Warning 'Потеря: часть записей вышла за пределы кольца ESP32.'
                        $warnedRingLoss = $true
                    }
                    $nextCursor = [uint64](@($response.Headers['X-Pulse-Cursor'])[0])
                    foreach ($line in ([string]$response.Content -split "`n")) {
                        $line = $line.Trim()
                        if (!$line) { continue }
                        $fields = $line.Split(',')
                        if ($fields.Length -ne 15) { throw "Некорректная запись импульса: $line" }
                        $sequence = [uint32]$fields[0]
                        $sourceDropped = [uint32]$fields[14]
                        if ($lastSequence -ne 0 -and $sequence -le $lastSequence) { continue }
                        if (($lastSequence -ne 0 -and $sequence -ne [uint32]($lastSequence + 1)) -or
                            $sourceDropped -gt 0) {
                            if (!$warnedSourceLoss) {
                                Write-Warning "Потеря импульсов источника обнаружена перед sequence=$sequence (source_dropped_before=$sourceDropped)."
                                $warnedSourceLoss = $true
                            }
                        }
                        $row = '{0},{1}' -f (Get-Date -Format 'o'), $line
                        [System.IO.File]::AppendAllText($fullPath, "$row`r`n", $utf8)
                        $lastSequence = $sequence
                    }
                    $cursor = $nextCursor
                } while ([string]$response.Content.Length -ge 1024)
            } catch {
                Write-Warning "Временная ошибка связи с кольцом импульсов: $($_.Exception.Message). Повтор через 500 мс."
                Start-Sleep -Milliseconds 500
                continue
            }
            Start-Sleep -Milliseconds 50
        }
    }
    'restart' {
        Invoke-RestMethod -Uri "$base$(Get-AgentPath 'restart' $deviceInfo)" -Method Post -TimeoutSec 5 |
            ConvertTo-Json -Depth 8
    }
    'flash' {
        $image = $Firmware
        if (!$image) { $image = $Operand }
        if (!$image) {
            # Optional convenience for this checkout; generic usage passes firmware.bin.
            $images = @(Get-ChildItem -LiteralPath (Join-Path $ProjectRoot 'build') -Filter '*.bin' -File |
                Where-Object { $_.Name -notin @('ota_data_initial.bin', 'bootloader.bin', 'partition-table.bin') })
            if ($images.Count -ne 1) { throw 'Укажите flash firmware.bin или -Firmware PATH.' }
            $image = $images[0].FullName
        }
        $image = (Resolve-Path -LiteralPath $image).Path
        $before = Test-Device $base
        if (!$before) { throw 'Не удалось прочитать информацию устройства перед OTA.' }
        Write-Host "OTA upload: $image -> $base"
        Invoke-WebRequest -Uri "$base$(Get-AgentPath 'ota' $before)" -Method Post -InFile $image `
            -ContentType 'application/octet-stream' -TimeoutSec 120 | Out-Null
        $after = Wait-AgentAfterFlash $base $before $RestartTimeoutSec
        [pscustomobject]@{
            flashed = $true
            device = $base
            previous_version = Get-FirmwareVersion $before
            current_version = Get-FirmwareVersion $after
            version = Get-FirmwareVersion $after
            version_changed = (Get-FirmwareVersion $before) -ne (Get-FirmwareVersion $after)
        } | ConvertTo-Json -Depth 8
    }
    default {
        Invoke-AgentCommand $base $Action.Replace('-', '_') | ConvertTo-Json -Depth 8
    }
}
