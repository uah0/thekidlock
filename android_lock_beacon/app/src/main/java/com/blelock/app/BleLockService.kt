package com.blelock.app

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.AlarmManager
import android.app.Service
import android.content.Intent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.Binder
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.SystemClock
import android.util.Log

class BleLockService : Service(), BleRepository.Listener {
    private val handler = Handler(Looper.getMainLooper())
    private lateinit var repository: BleRepository
    private lateinit var settings: LockSettings
    private lateinit var phoneBeacon: PhoneBeacon
    private var hotspotSessionId: String? = null
    private var openSentForPresence = false
    private val autoOpenPresenceGate = AutoOpenPresenceGate()
    private var autoOpenWritePending = false
    private var lastAutoOpenSuppressionLogMs = 0L
    private var autoOffClosePending = false
    private var autoOffCloseWriteInFlight = false
    private var workingModeClosePending = false
    private var stopping = false
    private var repositoryActive = false
    private var bluetoothAvailable = false
    private var reconnectAttempt = 0
    private var mode = Mode.AUTO_OPEN
    private val clients = LinkedHashSet<BleRepository.Listener>()
    private var lastLockInfo: String? = null
    private var lastLockStatus: String? = null
    private val binder = LocalBinder()

    inner class LocalBinder : Binder(), BleClient {
        fun registerClient(client: BleRepository.Listener) {
            clients.add(client)
            val cachedInfo = lastLockInfo
            val cachedStatus = lastLockStatus
            if (repository.currentState == BleRepository.State.READY &&
                (!cachedInfo.isNullOrBlank() || !cachedStatus.isNullOrBlank())
            ) {
                handler.post { client.onLockData(cachedInfo, cachedStatus) }
            }
        }

        fun unregisterClient(client: BleRepository.Listener) {
            clients.remove(client)
            if (clients.isEmpty() && !settings.autoOpenEnabled &&
                !settings.ownerAccessConfirmed && mode == Mode.ADMIN) {
                // ADMIN is also the connection-holding mode used while the main
                // screen is visible. Once the last UI client goes away and
                // automatic opening is disabled, there is no reason to retain
                // the GATT session or the started service.
                setAutomaticModeInternal(false)
            }
        }

        override val currentState: BleRepository.State
            get() = repository.currentState

        /** Read-only snapshot of the service's current operating mode. */
        val automaticModeActive: Boolean
            get() = mode == Mode.AUTO_OPEN

        override fun findAndConnect() = repository.findAndConnect()
        override fun requestLockStatusRefresh() = repository.requestLockStatusRefresh()
        override fun pollAdminState() = repository.pollAdminState()
        override fun retryAdminProbe() = repository.retryAdminProbe()
        override fun readAdminConfig() = repository.readAdminConfig()
        override fun readAdminSchema() = repository.readAdminSchema()
        override fun writeAdminConfig(value: String) = repository.writeAdminConfig(value)
        override fun readAdminBonds() = repository.readAdminBonds()
        override fun writeCurrentPhoneName() = repository.writeCurrentPhoneName()
        override fun readOtaConfig() = repository.readOtaConfig()
        override fun readOtaStatus() = repository.readOtaStatus()
        override fun writeOtaConfig(value: String) = repository.writeOtaConfig(value)
        override fun startHomeOta() = startHomeOtaInternal()
        override fun deleteBond(address: String) = repository.deleteBond(address)
        override fun deleteAllBonds() = repository.deleteAllBonds()
        override fun rebootEsp32() = repository.rebootEsp32()
        override fun setAutomaticMode(enabled: Boolean) {
            if (enabled) {
                autoOffClosePending = false
                autoOffCloseWriteInFlight = false
                setAutomaticModeInternal(true)
            } else {
                autoOffClosePending = false
                autoOffCloseWriteInFlight = false
                setAutomaticModeInternal(false)
            }
        }
        override fun openNow() = repository.openNow()
        override fun forceOpenNow() = repository.forceOpenNow()
        override fun toggleKeepOpen() = repository.toggleKeepOpen()
        override fun setWorkingMode(
            alwaysOpen: Boolean,
            closeWhenDisablingAlwaysOpen: Boolean
        ) {
            if (closeWhenDisablingAlwaysOpen) workingModeClosePending = true
            repository.setWorkingMode(alwaysOpen, closeWhenDisablingAlwaysOpen)
        }
        override fun close() = Unit

        fun requestAdminConnection() = requestAdminConnectionInternal()

        fun verifyOwnerAccess() {
            requestAdminConnectionInternal()
        }

        fun resumeAutomaticMode() = resumeAutomaticModeInternal()
    }
    private val bluetoothStateReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action == Intent.ACTION_SCREEN_OFF) {
                if (mode == Mode.ADMIN && settings.autoOpenEnabled) {
                    Log.i(TAG, "Screen off: restoring automatic BLE mode")
                    resumeAutomaticModeInternal()
                }
                return
            }
            if (intent.action != BluetoothAdapter.ACTION_STATE_CHANGED) return
            when (intent.getIntExtra(BluetoothAdapter.EXTRA_STATE, BluetoothAdapter.ERROR)) {
                BluetoothAdapter.STATE_OFF -> handleBluetoothOff()
                BluetoothAdapter.STATE_ON -> handleBluetoothOn()
            }
        }
    }
    private val phoneBeaconReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action == PhoneBeacon.ACTION_HOME_WIFI_FINISHED) {
                restoreBeaconMode()
                return
            }
            if (intent.action == PhoneBeacon.ACTION_HOLD_OPEN_RESULT) {
                lastHoldCommandPending = false
                lastHoldCommandError = intent.getStringExtra(PhoneBeacon.EXTRA_ERROR).orEmpty()
                restoreBeaconMode()
                return
            }
            if (intent.action == ACTION_CLEAR_HOTSPOT_IP) {
                val sessionId = intent.getStringExtra(EXTRA_HOTSPOT_SESSION)
                if (sessionId == null || sessionId != hotspotSessionId) return
                phoneBeacon.setHotspotIp(null)
                hotspotSessionId = null
                Log.i(TAG, "LocalOnlyHotspot stopped; PhoneBeacon hotspot IP cleared")
                if (phoneBeacon.isWifiProvisioningActive) {
                    phoneBeacon.cancelWifiProvisioning()
                    if (phoneBeacon.isHoldRequestActive || phoneBeacon.isHomeWifiRequestActive) {
                        restoreBeaconMode()
                        return
                    }
                    if (mode == Mode.WIFI_PROVISIONING) {
                        if (settings.autoOpenEnabled) {
                            mode = Mode.AUTO_OPEN
                            startAsForeground("Автоматический режим · BLE-маяк активен")
                            updateNotification("Автоматический режим · BLE-маяк активен")
                        } else {
                            mode = Mode.IDLE
                            stopSelfSafely()
                        }
                    }
                }
                if (mode == Mode.WEB_UI_DISCOVERY && !settings.autoOpenEnabled) {
                    phoneBeacon.finishWebUiIpRequest()
                    mode = Mode.IDLE
                    stopSelfSafely()
                }
                return
            }
            if (intent.action == PhoneBeacon.ACTION_WIFI_PROVISIONING_DISCONNECTED && (phoneBeacon.isHoldRequestActive || phoneBeacon.isHomeWifiRequestActive)) {
                restoreBeaconMode()
                return
            }
            if (intent.action != PhoneBeacon.ACTION_WIFI_PROVISIONING_DISCONNECTED ||
                mode != Mode.WIFI_PROVISIONING) return
            if (settings.autoOpenEnabled) {
                mode = Mode.AUTO_OPEN
                broadcastAutomaticMode(true)
                startAsForeground("Автоматический режим · BLE-маяк активен")
                updateNotification("Автоматический режим · BLE-маяк активен")
            } else if (phoneBeacon.isWebUiIpRequestActive) {
                mode = Mode.WEB_UI_DISCOVERY
                startAsForeground("Замок получил Wi-Fi · ожидаю Web UI IP")
                updateNotification("Замок получил Wi-Fi · ожидаю Web UI IP")
            } else {
                mode = Mode.IDLE
                stopSelfSafely()
            }
        }
    }
    private val departureRecovery = Runnable {
        if (!stopping && mode == Mode.AUTO_OPEN && openSentForPresence) {
            if (repository.hasNativeAutoConnectRequest) {
                Log.i(TAG, "DEPARTURE_RECOVERY_SKIPPED: native auto-connect and presence guard remain active")
                return@Runnable
            }
            openSentForPresence = false
            repository.findAndConnect()
        }
    }

    override fun onCreate() {
        super.onCreate()
        settings = LockSettings(this)
        phoneBeacon = PhoneBeacon(this)
        mode = if (settings.autoOpenEnabled) Mode.AUTO_OPEN else Mode.IDLE
        createNotificationChannel()
        // Publish the foreground notification immediately when this service
        // is restoring an enabled beacon. A UI-only bind with Auto Open OFF
        // must not start a background advertising/client session.
        if (settings.autoOpenEnabled) {
            startAsForeground(
                if (hasPermissions()) getString(R.string.service_starting)
                else getString(R.string.service_permissions_missing)
            )
        }
        repository = BleRepository(this, this, controlOnly = mode != Mode.ADMIN)
        repositoryActive = true
        bluetoothAvailable = getSystemService(BluetoothManager::class.java)
            .adapter?.isEnabled == true
        val stateFilter = IntentFilter().apply {
            addAction(BluetoothAdapter.ACTION_STATE_CHANGED)
            addAction(Intent.ACTION_SCREEN_OFF)
        }
        if (Build.VERSION.SDK_INT >= 33) {
            // Bluetooth broadcasts can originate from the privileged Bluetooth UID.
            registerReceiver(bluetoothStateReceiver, stateFilter, RECEIVER_EXPORTED)
            registerReceiver(phoneBeaconReceiver,
                IntentFilter().apply {
                    addAction(PhoneBeacon.ACTION_WIFI_PROVISIONING_DISCONNECTED)
                    addAction(PhoneBeacon.ACTION_HOLD_OPEN_RESULT)
                    addAction(PhoneBeacon.ACTION_HOME_WIFI_FINISHED)
                    addAction(ACTION_CLEAR_HOTSPOT_IP)
                }, RECEIVER_NOT_EXPORTED)
        } else {
            registerReceiver(bluetoothStateReceiver, stateFilter)
            registerReceiver(phoneBeaconReceiver,
                IntentFilter().apply {
                    addAction(PhoneBeacon.ACTION_WIFI_PROVISIONING_DISCONNECTED)
                    addAction(PhoneBeacon.ACTION_HOLD_OPEN_RESULT)
                    addAction(PhoneBeacon.ACTION_HOME_WIFI_FINISHED)
                    addAction(ACTION_CLEAR_HOTSPOT_IP)
                })
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        // A bound service survives stopSelf; a new start must revive command handling.
        stopping = false
        if (intent?.action == ACTION_AUTO_OPEN_TOGGLE) {
            // Widget taps start this service directly, including when AUTO is OFF.
            startAsForeground(getString(R.string.service_starting))
            val enabled = !settings.autoOpenEnabled
            if (enabled && !hasPermissions()) {
                android.widget.Toast.makeText(this, R.string.widget_permissions_missing,
                    android.widget.Toast.LENGTH_LONG).show()
                stopSelfSafely()
                return START_NOT_STICKY
            }
            settings.autoOpenEnabled = enabled
            autoOffClosePending = false
            autoOffCloseWriteInFlight = false
            setAutomaticModeInternal(enabled)
            Log.i(TAG, "Widget switched auto open -> $enabled")
            return if (enabled) START_STICKY else START_NOT_STICKY
        }
        if (intent?.action == ACTION_HOME_WIFI_REQUEST) {
            bluetoothAvailable = getSystemService(BluetoothManager::class.java).adapter?.isEnabled == true
            if (!hasPermissions() || !bluetoothAvailable || phoneBeacon.isServiceRequestActive)
                return START_NOT_STICKY
            if (repositoryActive) { repository.close(); repositoryActive = false }
            startAsForeground("Запрос домашнего Wi-Fi замка")
            phoneBeacon.requestHomeWifi()
            restoreBeaconMode()
            return START_NOT_STICKY
        }
        if (intent?.action == ACTION_HOLD_OPEN_TOGGLE) {
            bluetoothAvailable = getSystemService(BluetoothManager::class.java).adapter?.isEnabled == true
            if (!hasPermissions() || !bluetoothAvailable) {
                lastHoldCommandPending = false
                lastHoldCommandError = "Bluetooth или разрешения недоступны"
                sendBroadcast(Intent(PhoneBeacon.ACTION_HOLD_OPEN_RESULT).setPackage(packageName)
                    .putExtra(PhoneBeacon.EXTRA_SUCCESS, false).putExtra(PhoneBeacon.EXTRA_ERROR, lastHoldCommandError))
                return START_NOT_STICKY
            }
            if (!phoneBeacon.isHoldRequestActive) {
                if (repositoryActive) {
                    repository.close()
                    repositoryActive = false
                }
                lastHoldCommandPending = true
                lastHoldCommandError = ""
                startAsForeground("Ожидание подтверждения режима замка")
                phoneBeacon.requestToggleHoldOpen()
            }
            restoreBeaconMode()
            return START_NOT_STICKY
        }
        if (intent?.action == ACTION_BEACON_TX_POWER_CHANGED) {
            phoneBeacon.applyConfiguredTransmitPower()
            return if (mode == Mode.IDLE) {
                stopSelfResult(startId)
                START_NOT_STICKY
            } else {
                START_STICKY
            }
        }
        if (intent?.action == ACTION_WEB_UI_IP_REQUEST) {
            if (!hasPermissions() || !bluetoothAvailable) {
                Log.e(TAG, "Cannot request Web UI IPv4: BLE permission/radio unavailable")
                return START_NOT_STICKY
            }
            if (settings.autoOpenEnabled) {
                mode = Mode.AUTO_OPEN
                phoneBeacon.start()
                phoneBeacon.startWebUiIpRequest()
                broadcastAutomaticMode(true)
                startAsForeground("Автоматический режим · запрос Web UI IP")
                updateNotification("Автоматический режим · запрос Web UI IP")
            } else {
                mode = Mode.WEB_UI_DISCOVERY
                phoneBeacon.startWebUiIpRequest()
                startAsForeground("Ожидание Web UI IP от замка")
                updateNotification("Ожидание Web UI IP от замка")
            }
            return START_STICKY
        }
        if (intent?.action == ACTION_WEB_UI_IP_FINISHED) {
            phoneBeacon.finishWebUiIpRequest()
            if (settings.autoOpenEnabled) {
                mode = Mode.AUTO_OPEN
                phoneBeacon.start()
                broadcastAutomaticMode(true)
            } else if (!phoneBeacon.isServiceRequestActive) {
                mode = Mode.IDLE
                stopSelfSafely()
            }
            return START_NOT_STICKY
        }
        if (intent?.action == ACTION_WIFI_PROVISION_PENDING) {
            val sessionId = intent.getStringExtra(EXTRA_HOTSPOT_SESSION)
            if (!hasPermissions() || !bluetoothAvailable || sessionId.isNullOrBlank()) {
                Log.e(TAG, "Cannot prepare Wi-Fi provisioning: BLE permission/radio/session unavailable")
                return START_NOT_STICKY
            }
            mode = Mode.WIFI_PROVISIONING
            hotspotSessionId = sessionId
            if (repositoryActive) {
                repository.close()
                repositoryActive = false
            }
            phoneBeacon.prepareWifiProvisioning(settings.autoOpenEnabled)
            startAsForeground("Подготовка Wi-Fi · маяк AUTO приостановлен")
            updateNotification("Подготовка Wi-Fi · маяк AUTO приостановлен")
            Log.i(TAG, "AUTO beacon paused for hotspot preparation session=$sessionId")
            return START_STICKY
        }
        if (intent?.action == ACTION_WIFI_PROVISION) {
            val ssid = intent.getStringExtra(EXTRA_WIFI_SSID).orEmpty()
            val password = intent.getStringExtra(EXTRA_WIFI_PASSWORD).orEmpty()
            val hotspotIp = intent.getByteArrayExtra(EXTRA_HOTSPOT_IP)?.takeIf { it.size == 4 }
            val sessionId = intent.getStringExtra(EXTRA_HOTSPOT_SESSION)
            if (!hasPermissions() || !bluetoothAvailable || ssid.isBlank()) {
                Log.e(TAG, "Cannot start Wi-Fi provisioning: BLE permission/radio/SSID unavailable")
                stopSelfSafely()
                return START_NOT_STICKY
            }
            mode = Mode.WIFI_PROVISIONING
            hotspotSessionId = sessionId
            if (repositoryActive) {
                repository.close()
                repositoryActive = false
            }
            startAsForeground("Настройка Wi-Fi · ждём замок")
            runCatching { phoneBeacon.startWifiProvisioning(ssid, password, hotspotIp) }
                .onFailure {
                    Log.e(TAG, "Wi-Fi provisioning beacon failed", it)
                    stopSelfSafely()
                }
            return START_STICKY
        }
        if (intent?.action == ACTION_ADMIN) {
            Log.w(TAG, "Legacy ADMIN BLE request ignored; owner administration uses Wi-Fi Web UI")
            stopSelfSafely()
            return START_NOT_STICKY
        }
        if (intent?.action == ACTION_START) {
            if (phoneBeacon.isServiceRequestActive) {
                if (settings.autoOpenEnabled) phoneBeacon.start()
                return START_STICKY
            }
            mode = if (settings.autoOpenEnabled) Mode.AUTO_OPEN else Mode.IDLE
            if (settings.autoOpenEnabled) startAsForeground(getString(R.string.service_starting))
            broadcastAutomaticMode(mode == Mode.AUTO_OPEN)
        }
        if (intent?.action == ACTION_STOP || !settings.autoOpenEnabled) {
            stopSelfSafely()
            return START_NOT_STICKY
        }
        if (!hasPermissions()) {
            updateNotification(getString(R.string.service_permissions_missing))
            return START_NOT_STICKY
        }
        if (!bluetoothAvailable) {
            updateNotification(getString(R.string.bluetooth_disabled))
            return START_STICKY
        }
        if (mode == Mode.AUTO_OPEN) {
            phoneBeacon.start()
            if (repositoryActive) {
                repository.close()
                repositoryActive = false
            }
            startAsForeground("Автоматический режим · BLE-маяк активен")
            updateNotification("Автоматический режим · BLE-маяк активен")
            return START_STICKY
        }
        scheduleSearch(0)
        return START_STICKY
    }
    private val otaRecovery = Runnable {
        if (stopping || mode != Mode.OTA || !bluetoothAvailable) return@Runnable
        mode = when {
            settings.autoOpenEnabled -> Mode.AUTO_OPEN
            else -> Mode.IDLE
        }
        broadcastAutomaticMode(mode == Mode.AUTO_OPEN)
        openSentForPresence = false
        if (mode == Mode.AUTO_OPEN) {
            phoneBeacon.start()
            if (repositoryActive) {
                repository.close()
                repositoryActive = false
            }
            startAsForeground("Автоматический режим · BLE-маяк активен")
            updateNotification("Автоматический режим · BLE-маяк активен")
        } else if (mode == Mode.ADMIN) {
            if (!repositoryActive) {
                repository = BleRepository(this, this, controlOnly = false)
                repositoryActive = true
            } else {
                repository.setControlOnly(false)
            }
            scheduleSearch(0)
        }
    }

    override fun onDestroy() {
        stopping = true
        phoneBeacon.stop()
        lastHoldCommandPending = false
        handler.removeCallbacksAndMessages(null)
        if (repositoryActive) repository.close()
        runCatching { unregisterReceiver(bluetoothStateReceiver) }
        runCatching { unregisterReceiver(phoneBeaconReceiver) }
        clearCachedConnectionState()
        super.onDestroy()
    }

    override fun onTaskRemoved(rootIntent: Intent?) {
        if (settings.autoOpenEnabled && !stopping) {
            scheduleServiceRecovery()
        }
        super.onTaskRemoved(rootIntent)
    }

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onBleState(state: BleRepository.State, detail: String) {
        if (state != repository.currentState) {
            Log.d(TAG, "Ignoring stale BLE state callback: $state, current=${repository.currentState}")
            return
        }
        if (mode != Mode.ADMIN && mode != Mode.OTA) {
            // The Android phone is the Peripheral in the new architecture.
            // A delayed callback from a closed legacy client must not restart
            // scanning or send any lock command.
            Log.d(TAG, "Ignoring legacy Android Central callback in mode=$mode")
            return
        }
        updateNotification(detail)
        // Зелёным считаем весь период реальной GATT-сессии: с момента успешной
        // проверки забонденного ключа (READY) и до фактического разрыва связи.
        // OPEN_SENT — та же самая сессия, просто уже отправлена команда открытия;
        // WAITING_DEPARTURE сюда не входит, т.к. к этому моменту GATT уже закрыт.
        lastConnected = state == BleRepository.State.READY || state == BleRepository.State.OPEN_SENT
        sendBroadcast(
            Intent(ACTION_CONNECTION_STATE)
                .setPackage(packageName)
                .putExtra(EXTRA_CONNECTED, lastConnected)
        )
        clients.toList().forEach { it.onBleState(state, detail) }
        if (!bluetoothAvailable) return
        when (state) {
            BleRepository.State.READY -> {
                resetReconnectBackoff()
                if (autoOffClosePending) {
                    trySendAutoOffClose()
                } else if (mode == Mode.AUTO_OPEN) {
                    repository.requestImmediateRssi()
                }
            }
            BleRepository.State.OPEN_SENT -> {
                autoOpenWritePending = false
                if (detail == "Команда OPEN передана замку") {
                    openSentForPresence = true
                    autoOpenPresenceGate.onOpenCommandAccepted()
                }
            }
            BleRepository.State.IDLE, BleRepository.State.ERROR -> {
                autoOpenWritePending = false
                workingModeClosePending = false
                if (stopping) return
                if (mode == Mode.OTA) {
                    handler.removeCallbacks(otaRecovery)
                    handler.postDelayed(otaRecovery, OTA_RECOVERY_DELAY_MS)
                    return
                }
                if (state == BleRepository.State.ERROR) {
                    if (autoOffClosePending) autoOffCloseWriteInFlight = false
                    // A failed GATT session is not evidence that OPEN reached
                    // the lock. The next secured session must retry OPEN
                    // immediately instead of waiting for departure detection.
                    openSentForPresence = false
                    handler.removeCallbacks(departureRecovery)
                    if (mode == Mode.AUTO_OPEN || mode == Mode.ADMIN) {
                        scheduleReconnect()
                    }
                    return
                }
                if (openSentForPresence) {
                    handler.postDelayed({
                        repository.waitForDeparture()
                        // If ESP32 was rebooted while the phone stayed nearby,
                        // the old GATT session may not report a clean disconnect.
                        // Do not wait forever for a departure: re-discover the
                        // advertising lock and establish a fresh secured link.
                        handler.removeCallbacks(departureRecovery)
                        handler.postDelayed(departureRecovery, DEPARTURE_RECOVERY_MS)
                    }, DEPARTURE_SCAN_DELAY_MS)
                } else {
                    if (mode == Mode.AUTO_OPEN || mode == Mode.ADMIN) {
                        scheduleReconnect()
                    }
                }
            }
            BleRepository.State.DEPARTED -> {
                handler.removeCallbacks(departureRecovery)
                openSentForPresence = false
                autoOpenPresenceGate.onDepartureConfirmed()
                if (mode == Mode.AUTO_OPEN || mode == Mode.ADMIN) {
                    if (repository.hasNativeAutoConnectRequest) {
                        Log.i(TAG, "BLE departure confirmed; native auto-connect remains armed")
                    } else {
                        scheduleReconnect()
                    }
                }
            }
            else -> Unit
        }
    }

    override fun onLockData(info: String?, status: String?) {
        if (!info.isNullOrBlank()) lastLockInfo = info
        if (!status.isNullOrBlank()) lastLockStatus = status
        if (workingModeClosePending && status?.trim()?.equals("LOCKED", ignoreCase = true) == true) {
            workingModeClosePending = false
            openSentForPresence = false
            repository.clearAutoConnectOpenSentForPresence()
            Log.i(TAG, "Always Open reset confirmed LOCKED; native presence guard cleared")
        }
        if (autoOffClosePending && autoOffCloseWriteInFlight &&
            status?.trim()?.equals("LOCKED", ignoreCase = true) == true) {
            autoOffClosePending = false
            autoOffCloseWriteInFlight = false
            openSentForPresence = false
            Log.i(TAG, "Automatic mode disabled; lock closure confirmed")
            if (clients.isEmpty()) setAutomaticModeInternal(false)
        } else if (autoOffClosePending && autoOffCloseWriteInFlight &&
            !status.isNullOrBlank()) {
            // The command write is acknowledged before lock_task necessarily
            // processes its queue. Allow one more send after the status read.
            autoOffCloseWriteInFlight = false
            handler.postDelayed({ trySendAutoOffClose() }, AUTO_OFF_CLOSE_RETRY_MS)
        }
        if (info != null && status != null) {
            updateNotification("$info · $status")
        }
        clients.toList().forEach { it.onLockData(info, status) }
    }

    private fun trySendAutoOffClose() {
        if (!autoOffClosePending || autoOffCloseWriteInFlight ||
            !repositoryActive || !bluetoothAvailable) return
        if (repository.disableAutomaticModeAndClose()) {
            autoOffCloseWriteInFlight = true
            Log.i(TAG, "AUTO_OFF_CLOSE sent; waiting for LOCKED status")
        }
    }

    override fun onSignalRssi(rssi: Int?) {
        lastRssi = rssi
        sendBroadcast(
            Intent(ACTION_SIGNAL_RSSI)
                .setPackage(packageName)
                .putExtra(EXTRA_SIGNAL_VALID, rssi != null)
                .putExtra(EXTRA_SIGNAL_RSSI, rssi ?: 0)
        )
        clients.toList().forEach { it.onSignalRssi(rssi) }
    }

    override fun onGattRssi(rssi: Int?) {
        if (rssi == null) return
        // In AUTO the lock now initiates the secured contact from beacon data.
        // A stale legacy GATT callback must never send the former OPEN command.
        if (mode == Mode.AUTO_OPEN) return
        val threshold = settings.rssiThreshold
        val nativeRequest = repository.hasNativeAutoConnectRequest
        val nativeOpenSent = nativeRequest && repository.hasOpenBeenSentForAutoConnectPresence
        val nativeOpenPending = nativeRequest && repository.hasOpenPendingForAutoConnectPresence
        val suppressionReason = when {
            mode != Mode.AUTO_OPEN -> "mode=$mode"
            !settings.autoOpenEnabled -> "autoOpenEnabled=false"
            autoOffClosePending -> "autoOffClosePending=true"
            repository.currentState != BleRepository.State.READY -> "state=${repository.currentState}"
            autoOpenWritePending -> "autoOpenWritePending=true"
            autoOpenPresenceGate.isBlocked -> "one OPEN already sent for this presence"
            nativeOpenSent -> "nativeOpenSent=true"
            nativeOpenPending -> "nativeOpenPending=true"
            else -> null
        }
        if (suppressionReason != null) {
            val now = SystemClock.elapsedRealtime()
            if (rssi >= threshold && now - lastAutoOpenSuppressionLogMs >= 5_000L) {
                lastAutoOpenSuppressionLogMs = now
                Log.w(
                    TAG,
                    "AUTO_OPEN_SUPPRESSED: rssi=$rssi threshold=$threshold reason=$suppressionReason " +
                        "autoOpenWritePending=$autoOpenWritePending openSentForPresence=$openSentForPresence " +
                        "nativeRequest=$nativeRequest nativeOpenSent=$nativeOpenSent nativeOpenPending=$nativeOpenPending"
                )
            }
            return
        }

        if (rssi < threshold) {
            Log.i(TAG, "AUTO_OPEN_WAITING_FOR_RSSI: gattRssi=$rssi threshold=$threshold dBm")
            return
        }

        autoOpenWritePending = true
        Log.i(TAG, "AUTO_OPEN_RSSI_THRESHOLD_REACHED: gattRssi=$rssi threshold=$threshold dBm")
        repository.openNow()
    }

    override fun onConnectionLost() {
        clients.toList().forEach { it.onConnectionLost() }
    }

    override fun onAdminConfig(value: String) {
        clients.toList().forEach { it.onAdminConfig(value) }
    }

    override fun onAdminSchema(value: String) {
        clients.toList().forEach { it.onAdminSchema(value) }
    }

    override fun onAdminSchemaError(detail: String) {
        clients.toList().forEach { it.onAdminSchemaError(detail) }
    }

    override fun onAdminAvailability(available: Boolean) {
        clients.toList().forEach { it.onAdminAvailability(available) }
    }

    override fun onAdminState(value: String) {
        clients.toList().forEach { it.onAdminState(value) }
    }

    override fun onAdminBonds(value: String) {
        clients.toList().forEach { it.onAdminBonds(value) }
    }

    override fun onAdminEvents(value: String) {
        clients.toList().forEach { it.onAdminEvents(value) }
    }

    override fun onOtaConfig(value: String) {
        clients.toList().forEach { it.onOtaConfig(value) }
    }

    override fun onOtaStatus(value: String) {
        if (value == "ERROR_WIFI" || value == "ERROR_OTA") {
            mode = Mode.IDLE
            broadcastAutomaticMode(false)
        }
        clients.toList().forEach { it.onOtaStatus(value) }
    }

    private fun scheduleSearch(delayMs: Long) {
        handler.removeCallbacks(searchRunnable)
        handler.postDelayed(searchRunnable, delayMs)
    }

    private fun scheduleReconnect() {
        if (mode == Mode.ADMIN && clients.isNotEmpty()) {
            // The visible application must recover its control connection
            // promptly. Background automatic mode keeps exponential backoff to
            // avoid wasting the battery while the lock is genuinely absent.
            reconnectAttempt = 0
            Log.i(TAG, "BLE UI reconnect scheduled in ${UI_RECONNECT_DELAY_MS}ms")
            scheduleSearch(UI_RECONNECT_DELAY_MS)
            return
        }
        val exponent = reconnectAttempt.coerceAtMost(RECONNECT_BACKOFF_STEPS)
        val delay = (RECONNECT_DELAY_MS shl exponent).coerceAtMost(RECONNECT_MAX_DELAY_MS)
        reconnectAttempt = (reconnectAttempt + 1).coerceAtMost(RECONNECT_BACKOFF_STEPS + 1)
        Log.i(TAG, "BLE reconnect scheduled in ${delay}ms (attempt=$reconnectAttempt)")
        scheduleSearch(delay)
    }

    private fun resetReconnectBackoff() {
        if (reconnectAttempt != 0) {
            Log.i(TAG, "BLE reconnect backoff reset after READY")
        }
        reconnectAttempt = 0
    }

    private fun requestAdminConnectionInternal() {
        Log.w(TAG, "Запрос старого Android Central отклонён; BLE-регистрацию и AUTO инициирует замок")
    }

    private fun startHomeOtaInternal(): Boolean {
        if (stopping || mode != Mode.ADMIN || !repositoryActive) return false
        mode = Mode.OTA
        broadcastAutomaticMode(false)
        val started = repository.startHomeOta()
        if (!started) {
            mode = Mode.ADMIN
            broadcastAutomaticMode(false)
        }
        return started
    }

    private fun restoreBeaconMode() {
        mode = when {
            phoneBeacon.isWifiProvisioningActive -> Mode.WIFI_PROVISIONING
            phoneBeacon.isWebUiIpRequestActive -> Mode.WEB_UI_DISCOVERY
            phoneBeacon.isHoldRequestActive || phoneBeacon.isHomeWifiRequestActive -> Mode.SERVICE_REQUEST
            settings.autoOpenEnabled -> Mode.AUTO_OPEN
            else -> Mode.IDLE
        }
        if (mode == Mode.IDLE) {
            stopSelfSafely()
            return
        }
        if (mode == Mode.AUTO_OPEN) phoneBeacon.start()
        val message = if (phoneBeacon.isServiceRequestActive) "Служебный BLE-обмен с замком"
            else "Автоматический режим · BLE-маяк активен"
        startAsForeground(message)
        updateNotification(message)
        broadcastAutomaticMode(settings.autoOpenEnabled)
    }

    private fun resumeAutomaticModeInternal() {
        if (stopping || !bluetoothAvailable) return
        if (phoneBeacon.isServiceRequestActive) {
            if (settings.autoOpenEnabled) phoneBeacon.start() else phoneBeacon.stopAutoOpen()
            restoreBeaconMode()
            return
        }
        mode = if (settings.autoOpenEnabled) Mode.AUTO_OPEN else Mode.IDLE
        broadcastAutomaticMode(mode == Mode.AUTO_OPEN)
        openSentForPresence = false
        autoOpenWritePending = false
        if (mode == Mode.AUTO_OPEN) {
            phoneBeacon.start()
            if (repositoryActive) {
                repository.close()
                repositoryActive = false
            }
            startAsForeground("Автоматический режим · BLE-маяк активен")
            updateNotification("Автоматический режим · BLE-маяк активен")
            return
        }
        phoneBeacon.stop()
        if (repositoryActive) {
            repository.close()
            repositoryActive = false
        }
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    private fun setAutomaticModeInternal(enabled: Boolean) {
        if (stopping && enabled) return
        bluetoothAvailable = getSystemService(BluetoothManager::class.java).adapter?.isEnabled == true
        if (!enabled) {
            if (phoneBeacon.isServiceRequestActive) {
                phoneBeacon.stopAutoOpen()
                restoreBeaconMode()
                broadcastAutomaticMode(false)
                return
            }
            phoneBeacon.stopAutoOpen()
            openSentForPresence = false
            autoOpenWritePending = false
            handler.removeCallbacks(searchRunnable)
            handler.removeCallbacks(departureRecovery)
            broadcastAutomaticMode(false)

            mode = Mode.IDLE
            if (repositoryActive) {
                repository.close()
                repositoryActive = false
            }
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelf()
            return
        }

        if (phoneBeacon.isServiceRequestActive) {
            phoneBeacon.start()
            restoreBeaconMode()
            broadcastAutomaticMode(true)
            Log.i(TAG, "Auto Open preference changed independently; Wi-Fi provisioning continues")
            return
        }

        mode = Mode.AUTO_OPEN
        if (!hasPermissions()) {
            mode = Mode.IDLE
            broadcastAutomaticMode(false)
            updateNotification(getString(R.string.service_permissions_missing))
            return
        }
        if (!bluetoothAvailable) {
            broadcastAutomaticMode(true)
            updateNotification(getString(R.string.bluetooth_disabled))
            return
        }
        phoneBeacon.start()
        if (repositoryActive) {
            repository.close()
            repositoryActive = false
        }
            startAsForeground("Автоматический режим · BLE-маяк активен")
        broadcastAutomaticMode(true)
    }

    private val searchRunnable = Runnable {
        if (mode == Mode.AUTO_OPEN) return@Runnable
        if (stopping || (mode != Mode.AUTO_OPEN && mode != Mode.ADMIN) || !bluetoothAvailable) {
            Log.d(TAG, "BLE search skipped: stopping=$stopping mode=$mode bluetooth=$bluetoothAvailable")
            return@Runnable
        }
        if (!repositoryActive) {
            repository = BleRepository(this, this, controlOnly = mode != Mode.ADMIN)
            repositoryActive = true
            Log.i(TAG, "BLE repository recreated before reconnect")
        } else if (repository.currentState == BleRepository.State.ERROR) {
            Log.w(TAG, "BLE repository is in ERROR; replacing it before reconnect")
            repository.close()
            repository = BleRepository(this, this, controlOnly = mode != Mode.ADMIN)
            repositoryActive = true
        }
        if (!repository.isConnectingOrConnected()) {
            Log.i(TAG, "BLE reconnect scan started")
            repository.findAndConnect()
        } else {
            Log.d(TAG, "BLE search skipped: repository state=${repository.currentState}")
        }
    }

    private fun handleBluetoothOff() {
        phoneBeacon.stop()
        bluetoothAvailable = false
        resetReconnectBackoff()
        handler.removeCallbacks(searchRunnable)
        handler.removeCallbacks(departureRecovery)
        // The disconnect callback from the closing GATT is asynchronous. Clear
        // this first so that callback cannot start a stale departure scan which
        // would race the fresh repository created after Bluetooth STATE_ON.
        openSentForPresence = false
        autoOpenWritePending = false
        if (repositoryActive) {
            repository.close()
            repositoryActive = false
        }
        clearCachedConnectionState()
        broadcastAutomaticMode(false)
        updateNotification(getString(R.string.bluetooth_disabled))
    }

    private fun handleBluetoothOn() {
        bluetoothAvailable = true
        if (stopping || mode == Mode.IDLE) return
        resetReconnectBackoff()
        if (repositoryActive) {
            repository.close()
            repositoryActive = false
        }
        if (mode == Mode.AUTO_OPEN && settings.autoOpenEnabled) {
            phoneBeacon.start()
            openSentForPresence = false
            autoOpenWritePending = false
            broadcastAutomaticMode(true)
            startAsForeground("Автоматический режим · BLE-маяк активен")
            updateNotification("Автоматический режим · BLE-маяк активен")
            return
        }
        mode = Mode.IDLE
        stopSelfSafely()
    }

    private fun stopSelfSafely() {
        stopping = true
        phoneBeacon.stop()
        handler.removeCallbacksAndMessages(null)
        if (::repository.isInitialized && repositoryActive) repository.close()
        repositoryActive = false
        clearCachedConnectionState()
        broadcastAutomaticMode(false)
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    private fun clearCachedConnectionState() {
        lastConnected = false
        lastRssi = null
        sendBroadcast(
            Intent(ACTION_CONNECTION_STATE).setPackage(packageName).putExtra(EXTRA_CONNECTED, false)
        )
    }

    private fun broadcastAutomaticMode(active: Boolean) {
        Log.d(TAG, "automatic mode -> $active, mode=$mode")
        lastAutomaticMode = active
        sendBroadcast(
            Intent(ACTION_AUTOMATIC_MODE)
                .setPackage(packageName)
                .putExtra(EXTRA_AUTOMATIC_MODE, active)
        )
    }

    private fun scheduleServiceRecovery() {
        val restartIntent = Intent(this, ServiceRestartReceiver::class.java)
            .setAction(ServiceRestartReceiver.ACTION_RESTART_SERVICE)
            .setPackage(packageName)
        val pendingIntent = PendingIntent.getBroadcast(
            this,
            RESTART_REQUEST_CODE,
            restartIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        val alarmManager = getSystemService(AlarmManager::class.java)
        alarmManager.setAndAllowWhileIdle(
            AlarmManager.ELAPSED_REALTIME_WAKEUP,
            SystemClock.elapsedRealtime() + SERVICE_RECOVERY_DELAY_MS,
            pendingIntent
        )
    }

    private fun startAsForeground(text: String) {
        val notification = buildNotification(text)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(
                NOTIFICATION_ID,
                notification,
                ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
            )
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
    }

    private fun updateNotification(text: String) {
        getSystemService(NotificationManager::class.java)
            .notify(NOTIFICATION_ID, buildNotification(text))
    }

    private fun buildNotification(text: String): Notification {
        val openApp = PendingIntent.getActivity(
            this,
            0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        val stopService = PendingIntent.getService(
            this,
            1,
            Intent(this, BleLockService::class.java).setAction(ACTION_STOP),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        return Notification.Builder(this, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setContentTitle(getString(R.string.service_title))
            .setContentText(text)
            .setContentIntent(openApp)
            .setOngoing(true)
            .addAction(0, getString(R.string.stop_service), stopService)
            .build()
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                getString(R.string.service_channel),
                NotificationManager.IMPORTANCE_LOW
            )
            getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
        }
    }

    private fun hasPermissions(): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) {
            return checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) ==
                PackageManager.PERMISSION_GRANTED
        }
        return checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) ==
            PackageManager.PERMISSION_GRANTED &&
            checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) ==
            PackageManager.PERMISSION_GRANTED &&
            checkSelfPermission(Manifest.permission.BLUETOOTH_ADVERTISE) ==
            PackageManager.PERMISSION_GRANTED
    }

    companion object {
        private const val TAG = "BleLockService"
        const val ACTION_START = "com.blelock.app.action.START"
        const val ACTION_AUTO_OPEN_TOGGLE = "com.blelock.app.action.AUTO_OPEN_TOGGLE"
        const val ACTION_HOLD_OPEN_TOGGLE = "com.blelock.app.action.HOLD_OPEN_TOGGLE"
        const val ACTION_HOME_WIFI_REQUEST = "com.blelock.app.action.HOME_WIFI_REQUEST"
        @Volatile var lastHoldCommandPending = false
        @Volatile var lastHoldCommandError = ""
        const val ACTION_STOP = "com.blelock.app.action.STOP"
        const val ACTION_ADMIN = "com.blelock.app.action.ADMIN"
        const val ACTION_WIFI_PROVISION = "com.blelock.app.action.WIFI_PROVISION"
        const val ACTION_WIFI_PROVISION_PENDING = "com.blelock.app.action.WIFI_PROVISION_PENDING"
        const val ACTION_WEB_UI_IP_REQUEST = "com.blelock.app.action.WEB_UI_IP_REQUEST"
        const val ACTION_WEB_UI_IP_FINISHED = "com.blelock.app.action.WEB_UI_IP_FINISHED"
        const val ACTION_BEACON_TX_POWER_CHANGED = "com.blelock.app.action.BEACON_TX_POWER_CHANGED"
        const val ACTION_CLEAR_HOTSPOT_IP = "com.blelock.app.action.CLEAR_HOTSPOT_IP"
        const val EXTRA_WIFI_SSID = "wifi_ssid"
        const val EXTRA_WIFI_PASSWORD = "wifi_password"
        const val EXTRA_HOTSPOT_IP = "hotspot_ipv4_network_order"
        const val EXTRA_HOTSPOT_SESSION = "hotspot_session_id"
        const val ACTION_DEVICE_DETECTED = "com.blelock.app.action.DEVICE_DETECTED"
        const val EXTRA_DETECTED_DEVICE = "detected_device"
        const val ACTION_SIGNAL_RSSI = "com.blelock.app.action.SIGNAL_RSSI"
        const val EXTRA_SIGNAL_VALID = "signal_valid"
        const val EXTRA_SIGNAL_RSSI = "signal_rssi"
        const val ACTION_CONNECTION_STATE = "com.blelock.app.action.CONNECTION_STATE"
        const val EXTRA_CONNECTED = "connected"
        const val ACTION_AUTOMATIC_MODE = "com.blelock.app.action.AUTOMATIC_MODE"
        const val EXTRA_AUTOMATIC_MODE = "automatic_mode"

        /** Последнее известное состояние — читается активити сразу при подписке,
         *  чтобы не ждать следующей смены состояния, если фон уже подключён. */
        @Volatile
        var lastConnected: Boolean = false
            private set

        @Volatile
        var lastRssi: Int? = null
            private set

        @Volatile
        var lastAutomaticMode: Boolean = false
            private set
        private const val CHANNEL_ID = "ble_lock_service"
        private const val NOTIFICATION_ID = 1001
        private const val RECONNECT_DELAY_MS = 3_000L
        private const val UI_RECONNECT_DELAY_MS = 1_000L
        private const val RECONNECT_MAX_DELAY_MS = 30_000L
        private const val RECONNECT_BACKOFF_STEPS = 4
        /* Отдельная (короткая) пауза перед стартом пассивного скана в waitForDeparture():
         * это не «настоящий» реконнект (GATT + handshake), а лёгкий пассивный скан,
         * так что нет причин ждать столько же, сколько перед scheduleSearch(). Небольшая
         * задержка всё же оставлена, чтобы дать стеку осесть сразу после closeGatt(). */
        private const val DEPARTURE_SCAN_DELAY_MS = 300L
        // Android 6–11 reports STATE_ON before its BLE/GATT security state is
        // ready. ESP also needs up to three seconds to expire the old radio
        // link. Starting immediately creates a doomed first GATT session.
        private const val BLUETOOTH_RESTART_DELAY_MS = 8_000L
        private const val DEPARTURE_RECOVERY_MS = 30_000L
        private const val OTA_RECOVERY_DELAY_MS = 15_000L
        private const val SERVICE_RECOVERY_DELAY_MS = 5_000L
        private const val AUTO_OFF_CLOSE_RETRY_MS = 1_000L
        private const val RESTART_REQUEST_CODE = 1002
    }

    private enum class Mode { IDLE, AUTO_OPEN, ADMIN, OTA, WIFI_PROVISIONING, WEB_UI_DISCOVERY, SERVICE_REQUEST }
}
