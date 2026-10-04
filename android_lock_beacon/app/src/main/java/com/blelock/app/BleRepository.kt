package com.blelock.app

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattService
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import android.os.Handler
import android.os.SystemClock
import android.util.Log
import android.os.Looper
import android.util.Base64
import java.util.zip.CRC32

@SuppressLint("MissingPermission")
class BleRepository(
    private val context: Context,
    private val listener: Listener,
    private var controlOnly: Boolean = false
) : BleClient {
    interface Listener {
        fun onBleState(state: State, detail: String)
        fun onConnectionLost() {}
        fun onLockData(info: String?, status: String?)
        fun onSignalRssi(rssi: Int?) {}
        fun onGattRssi(rssi: Int?) {}
        fun onAdminConfig(value: String) {}
        fun onAdminSchema(value: String) {}
        fun onAdminSchemaError(detail: String) {}
        fun onAdminAvailability(available: Boolean) {}
        fun onAdminState(value: String) {}
        fun onAdminBonds(value: String) {}
        fun onAdminEvents(value: String) {}
        fun onOtaConfig(value: String) {}
        fun onOtaStatus(value: String) {}
    }

    enum class State {
        IDLE, SCANNING, CONNECTING, BONDING, DISCOVERING, READY, OPEN_SENT,
        WAITING_DEPARTURE, DEPARTED, ERROR
    }

    private val handler = Handler(Looper.getMainLooper())
    private var persistentOtaConfigWritePending = false
    private val bluetoothManager = context.getSystemService(BluetoothManager::class.java)
    private val lockSettings = LockSettings(context)
    private var gatt: BluetoothGatt? = null
    private var autoConnectGattActive = false
    private var gattConnectionEstablished = false
    private var autoConnectLinkConnected = false
    private var autoConnectOpenWritePending = false
    private var secureStatusProbeAttempts = 0
    private var secureStatusProbeRetry: Runnable? = null
    private var connectWatchdog: Runnable? = null
    private var lockService: BluetoothGattService? = null
    private var state = State.IDLE
    private var info: String? = null
    private var status: String? = null
    private var pendingLockCommand = "OPEN"

    /** Текущее состояние подключения, доступное снаружи без изменения его. */
    override val currentState: State get() = state

    override fun requestLockStatusRefresh(): Boolean {
        if (state != State.READY) return false
        val characteristic = lockService?.getCharacteristic(BleProtocol.STATUS_UUID) ?: return false
        return enqueueGattOperation("LOCK_STATUS_REFRESH") {
            gatt?.readCharacteristic(characteristic) == true
        }
    }

    fun setControlOnly(value: Boolean) {
        controlOnly = value
        if (value) stopSignalMonitoring()
    }

    fun promoteToFullControl() {
        controlOnly = false
        if (state == State.READY) readInfo()
    }

    private enum class GattOperationResult { SUCCESS, FAILED, TIMEOUT, CANCELLED }

    override fun setAutomaticMode(enabled: Boolean) {
        controlOnly = enabled
        if (enabled) stopSignalMonitoring()
    }

    /** true, если уже идёт поиск/подключение/соединение установлено — новый findAndConnect() не нужен.
     *  DEPARTED сюда не входит: это завершённое состояние («уход подтверждён»),
     *  которое как раз ждёт вызова findAndConnect() для нового цикла поиска. */
    fun isConnectingOrConnected(): Boolean =
        (state != State.IDLE && state != State.ERROR && state != State.DEPARTED) ||
            hasNativeAutoConnectRequest

    val hasNativeAutoConnectRequest: Boolean
        get() = autoConnectGattActive && gatt != null
    val hasOpenBeenSentForAutoConnectPresence: Boolean
        get() = autoConnectGattActive && autoConnectOpenSentForPresence
    val hasOpenPendingForAutoConnectPresence: Boolean
        get() = autoConnectGattActive && autoConnectOpenWritePending

    fun clearAutoConnectOpenSentForPresence() {
        if (autoConnectGattActive) {
            autoConnectOpenSentForPresence = false
            autoConnectOpenWritePending = false
        }
    }
    private var departureMode = false
    private var departureScanActive = false
    private var connectionScanActive = false
    private var pendingConnectionScan: Runnable? = null
    private var advertisementTonePlayedForPresence = false
    private var lastBelowThresholdLogMs = 0L
    private var autoConnectOpenSentForPresence = false
    private var adminProbePending = false
    private var mtuNegotiationRetries = 0
    private var mtuRetryWatchdog: Runnable? = null
    private var postReadyMtuPending = false
    private var signalMonitoring = false
    private var refreshBondsAfterControl = false
    /* Строка, только что отправленная в CHR_ADMIN_CONFIG, ожидающая подтверждения
     * чтением. null, когда подтверждать нечего (обычное чтение по кнопке). */
    private var pendingConfigVerification: String? = null
    private var schemaControlPending = false
    private var schemaChunks: Array<ByteArray?>? = null
    private var schemaExpectedLength = 0
    private var schemaExpectedCrc = 0L
    private var schemaExpectedCount = 0
    private var pendingBondDevice: BluetoothDevice? = null

    private data class GattOperation(
        val name: String,
        val start: () -> Boolean
    )

    private val gattQueue = ArrayDeque<GattOperation>()
    private var activeGattOperation: GattOperation? = null
    private var activeGattTimeout: Runnable? = null
    private var mtuHandshakeComplete = false


    private fun enqueueGattOperation(name: String, start: () -> Boolean): Boolean {
        if (gatt == null || lockService == null || state == State.IDLE ||
            state == State.ERROR || state == State.DEPARTED) return false
        gattQueue.addLast(GattOperation(name, start))
        pumpGattQueue()
        return true
    }

    private fun hasPendingGattOperation(name: String): Boolean =
        activeGattOperation?.name == name || gattQueue.any { it.name == name }

    private fun pumpGattQueue() {
        if (activeGattOperation != null) return
        val operation = gattQueue.removeFirstOrNull() ?: return
        activeGattOperation = operation
        if (!operation.start()) {
            finishGattOperation(GattOperationResult.FAILED, pumpNext = false)
            if (operation.name == "LOCK_STATUS" && autoConnectGattActive && controlOnly) {
                scheduleSecureStatusProbeRetry("start rejected")
                return
            }
            if (autoConnectOpenWritePending &&
                operation.name == "WRITE:${BleProtocol.COMMAND_UUID}") {
                autoConnectOpenWritePending = false
            }
            fail("Не удалось запустить GATT-операцию: ${operation.name}")
            gattQueue.clear()
            return
        }
        val timeout = Runnable {
            if (activeGattOperation !== operation) return@Runnable
            finishGattOperation(GattOperationResult.TIMEOUT, pumpNext = false)
            if (operation.name == "LOCK_STATUS" && autoConnectGattActive && controlOnly) {
                scheduleSecureStatusProbeRetry("callback timeout")
                return@Runnable
            }
            if (autoConnectOpenWritePending &&
                operation.name == "WRITE:${BleProtocol.COMMAND_UUID}") {
                autoConnectOpenWritePending = false
            }
            gattQueue.clear()
            closeGatt()
            fail("Таймаут GATT-операции: ${operation.name}")
        }
        activeGattTimeout = timeout
        val timeoutMs = if (operation.name == "LOCK_INFO" && state == State.DISCOVERING) {
            INITIAL_SECURE_READ_TIMEOUT_MS
        } else {
            GATT_OPERATION_TIMEOUT_MS
        }
        handler.postDelayed(timeout, timeoutMs)
    }

    private fun finishGattOperation(result: GattOperationResult, pumpNext: Boolean) {
        val operation = activeGattOperation ?: return
        activeGattTimeout?.let(handler::removeCallbacks)
        activeGattTimeout = null
        activeGattOperation = null
        Log.d(TAG, "GATT operation ${operation.name} -> $result")
        if (pumpNext) pumpGattQueue()
    }

    private fun completeGattOperation(success: Boolean) {
        finishGattOperation(
            if (success) GattOperationResult.SUCCESS else GattOperationResult.FAILED,
            pumpNext = true
        )
    }

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            if (result.scanRecord?.serviceUuids?.contains(BleProtocol.SERVICE_PARCEL_UUID) != true) {
                return
            }
            if (state != State.SCANNING && state != State.WAITING_DEPARTURE) return
            if (!connectionScanActive) return
            if (state == State.SCANNING) {
                handler.removeCallbacks(connectionScanWatchdog)
                handler.postDelayed(connectionScanWatchdog, SCAN_SILENCE_RETRY_MS)
            }
            handler.post { listener.onSignalRssi(result.rssi) }
            if (departureMode) {
                if (!advertisementTonePlayedForPresence) {
                    advertisementTonePlayedForPresence = true
                    Log.i(TAG, "BLE lock advertisement detected during departure monitoring")
                    BleEventTones.advertisementDetected(context)
                }
                if (autoConnectGattActive && gatt != null) {
                    if (!autoConnectLinkConnected) scheduleDepartureConfirmation()
                    return
                }
                scheduleDepartureConfirmation()
            }
            val threshold = lockSettings.rssiThreshold
            if (result.rssi <= threshold) {
                val now = SystemClock.elapsedRealtime()
                if (now - lastBelowThresholdLogMs >= 5_000L) {
                    lastBelowThresholdLogMs = now
                    Log.i(TAG, "BLE_CONNECT_WAITING_FOR_RSSI: advRssi=${result.rssi} threshold=$threshold dBm")
                }
                return
            }
            if (departureMode) {
                departureMode = false
                stopScan()
                lockSettings.lockAddress = result.device.address
                pairOrConnect(result.device)
                return
            }
            Log.i(TAG, "BLE advertisement accepted for GATT connection: ${result.device.address}, rssi=${result.rssi}, threshold=$threshold")
            playAdvertisementToneOnceForPresence()
            stopScan()
            lockSettings.lockAddress = result.device.address
            pairOrConnect(result.device)
        }

        override fun onScanFailed(errorCode: Int) {
            if (state == State.SCANNING &&
                errorCode == ScanCallback.SCAN_FAILED_SCANNING_TOO_FREQUENTLY) {
                Log.w(TAG, "BLE scan throttled by Android; retrying after cooldown")
                stopScan()
                scheduleConnectionScan(SCAN_THROTTLE_RETRY_MS)
                return
            }
            if (departureMode && autoConnectGattActive) {
                departureScanActive = false
                Log.w(TAG, "Native auto-connect remains armed; departure scan failed: $errorCode")
                return
            }
            fail("Ошибка BLE scan: $errorCode")
        }
    }

    private val signalScanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            // Пассивный скан — только для живого отображения RSSI. Он присылает
            // замеры пачками (рекламные пакеты раз в 100-1000мс), и обычное
            // Реклама используется только для обнаружения устройства и запуска
            // нового цикла подключения после ухода.
            handler.post { listener.onSignalRssi(result.rssi) }
        }

        override fun onScanFailed(errorCode: Int) {
            signalMonitoring = false
            Log.w(TAG, "Signal-only BLE scan failed: $errorCode")
            handler.post { listener.onSignalRssi(null) }
        }
    }

    private val bondReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action != BluetoothDevice.ACTION_BOND_STATE_CHANGED) return
            val device = if (Build.VERSION.SDK_INT >= 33) {
                intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE, BluetoothDevice::class.java)
            } else {
                @Suppress("DEPRECATION")
                intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
            }
            if (device == null) return
            val newBondState = intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, BluetoothDevice.BOND_NONE)
            val prevBondState = intent.getIntExtra(BluetoothDevice.EXTRA_PREVIOUS_BOND_STATE, -1)
            Log.d(TAG, "ACTION_BOND_STATE_CHANGED addr=${device?.address} prev=$prevBondState new=$newBondState (pendingAddr=${pendingBondDevice?.address}, gattAddr=${gatt?.device?.address})")
            if (device?.address == pendingBondDevice?.address) {
                when (newBondState) {
                    BluetoothDevice.BOND_BONDED -> {
                        pendingBondDevice = null
                        connect(device)
                    }
                    BluetoothDevice.BOND_NONE -> if (prevBondState == BluetoothDevice.BOND_BONDING) {
                        pendingBondDevice = null
                        fail("Bonding отменён или завершился ошибкой")
                    }
                }
                return
            }
            if (device?.address != gatt?.device?.address) return
            when (newBondState) {
                BluetoothDevice.BOND_BONDED -> {
                    update(State.DISCOVERING, "Bonding завершён, проверка сервиса")
                    if (lockService != null) readInfo() else gatt?.discoverServices()
                }
                BluetoothDevice.BOND_NONE -> if (state == State.BONDING) {
                    fail("Bonding отменён или завершился ошибкой")
                }
            }
        }
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, statusCode: Int, newState: Int) {
            if (this@BleRepository.gatt !== gatt) {
                Log.w(TAG, "Ignoring stale GATT connection callback")
                return
            }
            connectWatchdog?.let(handler::removeCallbacks)
            connectWatchdog = null
            Log.d(TAG, "onConnectionStateChange gattHash=${System.identityHashCode(gatt)} status=$statusCode newState=$newState bondState=${gatt.device.bondState}")
            if (autoConnectGattActive && newState == BluetoothProfile.STATE_DISCONNECTED) {
                handleAutoConnectDisconnect(statusCode)
                return
            }
            if (statusCode != BluetoothGatt.GATT_SUCCESS) {
                playDisconnectToneIfConnected()
                handler.post { listener.onConnectionLost() }
                fail("Ошибка GATT connection: $statusCode")
                closeGatt()
                return
            }
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    if (autoConnectGattActive) autoConnectLinkConnected = true
                    if (autoConnectGattActive) playAdvertisementToneOnceForPresence()
                    if (departureMode) {
                        handler.removeCallbacks(departureConfirmed)
                    }
                    if (!gattConnectionEstablished) {
                        gattConnectionEstablished = true
                        Log.i(TAG, "GATT_CONNECTED: ${gatt.device.address}")
                    }
                    update(State.DISCOVERING, "Подключено, поиск GATT-сервиса")
                    gatt.discoverServices()
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
                    playDisconnectToneIfConnected()
                    handler.post { listener.onConnectionLost() }
                    stopRssiPolling()
                    setAdminAvailability(false)
                    update(State.IDLE, "Замок отключён")
                    closeGatt()
                }
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, statusCode: Int) {
            if (!isGattCallbackCurrent(gatt)) {
                Log.w(TAG, "Ignoring stale GATT services callback")
                return
            }
            Log.d(TAG, "onServicesDiscovered gattHash=${System.identityHashCode(gatt)} status=$statusCode bondState=${gatt.device.bondState}")
            if (statusCode != BluetoothGatt.GATT_SUCCESS) {
                fail("Service discovery завершился с ошибкой: $statusCode")
                return
            }
            lockService = gatt.getService(BleProtocol.SERVICE_UUID)
            if (lockService == null) {
                fail("Сервис BLE Lock не найден")
                return
            }
            secureStatusProbeRetry?.let(handler::removeCallbacks)
            secureStatusProbeRetry = null
            secureStatusProbeAttempts = 0
            mtuHandshakeComplete = false
            mtuNegotiationRetries = 0
            /* По умолчанию ATT MTU = 23 байта (~20 байт полезной нагрузки на запись).
             * Строка admin-конфигурации (rssi, таймеры, gpio, мощность маяка) длиннее
             * этого лимита и будет молча обрезана без ошибки на некоторых стеках, если
             * не договориться о большем MTU. Прошивка поддерживает до 256 байт
             * (CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=256). Инициатором обмена MTU по
             * спецификации BLE должен быть центральное устройство (телефон). */
            // Protected lock frames fit the default MTU. Establish encryption
            // and READY first on every Android version; requesting MTU before
            // security is restored returns status=6 on Android 16 and can hang
            // after a radio restart on Android 11.
            handler.postDelayed({
                if (isGattCallbackCurrent(gatt)) {
                    proceedAfterMtuNegotiation(gatt)
                }
            }, SECURITY_SETTLE_DELAY_MS)
        }

        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, statusCode: Int) {
            if (!isGattCallbackCurrent(gatt)) {
                Log.w(TAG, "Ignoring stale GATT MTU callback")
                return
            }
            mtuRetryWatchdog?.let(handler::removeCallbacks)
            mtuRetryWatchdog = null
            Log.d(TAG, "onMtuChanged mtu=$mtu status=$statusCode")
            if (postReadyMtuPending) {
                postReadyMtuPending = false
                // READY and the encrypted session are already established.
                // MTU is only an optimisation for larger admin values, so a
                // failure here must not restart the usable lock connection.
                adminProbePending = true
                handler.post { readAdminState() }
                return
            }
            if (statusCode != BluetoothGatt.GATT_SUCCESS &&
                mtuNegotiationRetries < MAX_MTU_NEGOTIATION_RETRIES) {
                mtuNegotiationRetries++
                handler.postDelayed({
                    if (isGattCallbackCurrent(gatt)) {
                        val started = gatt.requestMtu(REQUESTED_MTU)
                        Log.i(TAG, "MTU retry $mtuNegotiationRetries/" +
                            "$MAX_MTU_NEGOTIATION_RETRIES started=$started")
                        if (started) {
                            val retryNumber = mtuNegotiationRetries
                            val watchdog = Runnable {
                                if (isGattCallbackCurrent(gatt) &&
                                    !mtuHandshakeComplete &&
                                    mtuNegotiationRetries == retryNumber) {
                                    Log.w(TAG, "MTU retry callback timeout; reconnecting GATT")
                                    fail("Повтор MTU не завершён, быстрое переподключение")
                                    closeGatt()
                                }
                            }
                            mtuRetryWatchdog = watchdog
                            handler.postDelayed(watchdog, MTU_RETRY_CALLBACK_TIMEOUT_MS)
                        } else {
                            fail("Не удалось повторить MTU, быстрое переподключение")
                            closeGatt()
                        }
                    }
                }, MTU_RETRY_DELAY_MS)
                return
            }
            if (statusCode == BluetoothGatt.GATT_SUCCESS) mtuNegotiationRetries = 0
            // Android BLE callbacks are delivered from the Bluetooth binder
            // thread. Start the next ATT operation only on the repository
            // handler to avoid issuing readCharacteristic() from that thread.
            // A successful MTU callback can arrive before the controller has
            // finished restoring encryption for an existing bond (especially
            // immediately after the peripheral rebooted). Starting an
            // encrypted read in that window produces 129/133 and tears down an
            // otherwise healthy link. Give the security state a bounded settle
            // window before the first protected ATT operation.
            val delay = if (statusCode == BluetoothGatt.GATT_SUCCESS) {
                SECURITY_SETTLE_DELAY_MS
            } else {
                MTU_FALLBACK_DELAY_MS
            }
            handler.postDelayed({
                if (isGattCallbackCurrent(gatt)) {
                    proceedAfterMtuNegotiation(gatt)
                }
            }, delay)
        }

        @Deprecated("Used for Android 8-12 callback compatibility")
        override fun onCharacteristicRead(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            statusCode: Int
        ) {
            if (!isGattCallbackCurrent(gatt)) return
            handler.post { completeGattOperation(statusCode == BluetoothGatt.GATT_SUCCESS) }
            handleRead(characteristic.uuid, characteristic.value ?: byteArrayOf(), statusCode)
        }

        override fun onCharacteristicRead(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
            statusCode: Int
        ) {
            if (!isGattCallbackCurrent(gatt)) return
            handler.post { completeGattOperation(statusCode == BluetoothGatt.GATT_SUCCESS) }
            handleRead(characteristic.uuid, value, statusCode)
        }

        override fun onCharacteristicWrite(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            statusCode: Int
        ) {
            if (!isGattCallbackCurrent(gatt)) return
            handler.post { completeGattOperation(statusCode == BluetoothGatt.GATT_SUCCESS) }
            Log.d(TAG, "onCharacteristicWrite uuid=${characteristic.uuid} status=$statusCode")
            if (characteristic.uuid == BleProtocol.ADMIN_CONFIG_UUID) {
                if (statusCode == BluetoothGatt.GATT_SUCCESS) {
                    /* GATT-успех означает только то, что замок принял ATT-запись.
                     * Он не гарантирует, что записанные байты дошли целиком и что
                     * замок сохранил именно те значения, которые мы отправили.
                     * Поэтому сразу же перечитываем конфигурацию и сверяем её
                     * с тем, что отправили — см. handleRead(). */
                    update(State.DISCOVERING, "Настройки записаны, проверяю чтением...")
                    handler.postDelayed({ readAdminConfig() }, 300)
                } else {
                    pendingConfigVerification = null
                    fail("Настройки отклонены замком")
                }
                return
            }
            if (characteristic.uuid == BleProtocol.ADMIN_CONTROL_UUID) {
                if (schemaControlPending) {
                    schemaControlPending = false
                    if (statusCode == BluetoothGatt.GATT_SUCCESS) {
                        handler.post { readAdminSchemaChunk() }
                    } else {
                        schemaError("Не удалось выбрать фрагмент схемы, GATT status: $statusCode")
                    }
                    return
                }
                if (statusCode == BluetoothGatt.GATT_SUCCESS) {
                    update(State.READY, "Команда управления принята замком")
                    if (refreshBondsAfterControl) handler.post { readAdminBonds() }
                } else {
                    fail("Команда управления отклонена, GATT status: $statusCode")
                }
                refreshBondsAfterControl = false
                return
            }
            if (characteristic.uuid == BleProtocol.OTA_CONFIG_UUID) {
                val persistent = persistentOtaConfigWritePending
                persistentOtaConfigWritePending = false
                update(State.READY, if (statusCode == BluetoothGatt.GATT_SUCCESS) {
                    if (persistent) "Wi-Fi/OTA настройки сохранены в замке"
                    else "Временные настройки точки доступа переданы"
                } else {
                    if (persistent) "Wi-Fi/OTA настройки не сохранены, GATT status: $statusCode"
                    else "Настройки точки доступа отклонены, GATT status: $statusCode"
                })
                return
            }
            if (characteristic.uuid == BleProtocol.PHONE_NAME_UUID) {
                if (statusCode == BluetoothGatt.GATT_SUCCESS) {
                    update(State.READY, "Имя телефона сохранено")
                } else {
                    update(State.READY, "Имя телефона не сохранено, GATT status: $statusCode")
                }
                return
            }
            if (characteristic.uuid != BleProtocol.COMMAND_UUID) return
            if (statusCode == BluetoothGatt.GATT_SUCCESS) {
                if (autoConnectGattActive && pendingLockCommand == "OPEN") {
                    autoConnectOpenWritePending = false
                }
                if (autoConnectGattActive && pendingLockCommand == "OPEN") {
                    autoConnectOpenSentForPresence = true
                    Log.i(TAG, "OPEN_SENT: command write accepted for this presence")
                }
                update(State.OPEN_SENT, "Команда $pendingLockCommand передана замку")
                handler.postDelayed({ readStatus() }, 500)
            } else {
                if (autoConnectGattActive && pendingLockCommand == "OPEN") {
                    autoConnectOpenWritePending = false
                    Log.w(TAG, "OPEN write failed on native auto-connect; retaining request for secure retry")
                    update(State.CONNECTING, "Ожидание защищённого повтора записи OPEN")
                    scheduleSecureStatusProbeRetry("OPEN write status=$statusCode")
                    return
                }
                fail("ESP32 отклонила $pendingLockCommand, GATT status: $statusCode")
            }
        }

        override fun onReadRemoteRssi(gatt: BluetoothGatt, rssi: Int, statusCode: Int) {
            if (!isGattCallbackCurrent(gatt)) return
            val validRssi = if (statusCode == BluetoothGatt.GATT_SUCCESS) rssi else null
            handler.post {
                completeGattOperation(statusCode == BluetoothGatt.GATT_SUCCESS)
                listener.onGattRssi(validRssi)
                listener.onSignalRssi(validRssi)
                // GATT-опрос остаётся резервным источником: если фактически все 3 BLE-слота
                // замка заняты, advertising приостанавливается, и пассивный скан временно
                // перестаёт что-либо ловить — тогда опрос подхватывает RSSI.
                scheduleRssiPoll()
            }
        }
    }

    private fun proceedAfterMtuNegotiation(gatt: BluetoothGatt) {
        if (mtuHandshakeComplete) return
        mtuHandshakeComplete = true
        if (gatt.device.bondState != BluetoothDevice.BOND_BONDED) {
            update(State.BONDING, "Запуск системного Pairing/Bonding")
            if (!gatt.device.createBond()) {
                fail("Не удалось запустить системный Pairing/Bonding")
            }
        } else if (controlOnly) {
            // READY is published only after a successful READ_ENC response.
            // This proves the encrypted link before the service sends OPEN.
            Log.i(TAG, "AUTO_CONNECT_SECURE_PROBE_START: reading encrypted lock status")
            readStatus()
        } else {
            readInfo()
        }
    }

    init {
        val filter = IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED)
        if (Build.VERSION.SDK_INT >= 33) {
            context.registerReceiver(bondReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            @Suppress("DEPRECATION")
            context.registerReceiver(bondReceiver, filter)
        }
    }

    override fun findAndConnect() {
        if (isConnectingOrConnected()) {
            Log.i(TAG, "ACTIVE_CONNECT_SKIPPED_ALREADY_RUNNING: state=$state native=$hasNativeAutoConnectRequest")
            return
        }
        closeGatt()
        departureMode = false
        if (controlOnly) {
            val savedAddress = lockSettings.lockAddress
            val bondedDevice = runCatching {
                val address = savedAddress ?: return@runCatching null
                val adapter = bluetoothManager?.adapter ?: return@runCatching null
                adapter.getRemoteDevice(address)
                    .takeIf { it.bondState == BluetoothDevice.BOND_BONDED }
            }.getOrNull()
            if (bondedDevice != null) {
                stopScan()
                stopSignalMonitoring()
                Log.i(TAG, "NATIVE_AUTO_CONNECT_START: bonded device=$savedAddress")
                connect(bondedDevice, autoConnect = true)
                return
            }
            Log.i(TAG, "NATIVE_AUTO_CONNECT_FALLBACK: no saved bonded device; using filtered scan")
        }
        update(State.SCANNING, "Поиск BLE Lock…")
        scheduleConnectionScan()
    }

    private fun scheduleConnectionScan(extraDelayMs: Long = 0L) {
        if (state != State.SCANNING) return
        pendingConnectionScan?.let(handler::removeCallbacks)
        val now = SystemClock.elapsedRealtime()
        val delay = maxOf(extraDelayMs, lastConnectionScanStartMs + SCAN_START_INTERVAL_MS - now, 0L)
        val start = Runnable { startConnectionScan() }
        pendingConnectionScan = start
        if (delay > 0L) Log.i(TAG, "BLE scan start delayed ${delay}ms to avoid Android throttling")
        handler.postDelayed(start, delay)
    }

    private fun startConnectionScan() {
        pendingConnectionScan = null
        if (state != State.SCANNING) return
        val scanner = bluetoothManager?.adapter?.bluetoothLeScanner
        if (scanner == null) {
            fail("Bluetooth выключен или BLE scanner недоступен")
            return
        }
        // Фильтр по рекламируемому имени работает при выключенном экране.
        // MAC может измениться из-за BLE privacy, а аппаратный фильтр по
        // 128-битному UUID терял пакеты на Xiaomi. UUID проверяется в callback.
        val filter = ScanFilter.Builder().setDeviceName(BleProtocol.DEVICE_NAME).build()
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()
        lastConnectionScanStartMs = SystemClock.elapsedRealtime()
        connectionScanActive = true
        scanner.startScan(listOf(filter), settings, scanCallback)
        handler.removeCallbacks(connectionScanWatchdog)
        handler.postDelayed(connectionScanWatchdog, SCAN_SILENCE_RETRY_MS)
    }

    private val connectionScanWatchdog = Runnable {
        if (state != State.SCANNING) return@Runnable
        Log.w(TAG, "BLE scan produced no lock advertisements; restarting scanner")
        stopScan()
        scheduleConnectionScan()
    }

    fun startSignalMonitoring() {
        if (controlOnly || signalMonitoring || departureScanActive || departureMode) return
        val scanner = bluetoothManager?.adapter?.bluetoothLeScanner ?: return
        val filter = ScanFilter.Builder().setServiceUuid(BleProtocol.SERVICE_PARCEL_UUID).build()
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_POWER)
            .build()
        runCatching {
            scanner.startScan(listOf(filter), settings, signalScanCallback)
            signalMonitoring = true
        }
    }

    fun stopSignalMonitoring() {
        if (!signalMonitoring) return
        signalMonitoring = false
        runCatching {
            bluetoothManager?.adapter?.bluetoothLeScanner?.stopScan(signalScanCallback)
        }
    }

    fun waitForDeparture() {
        if (!autoConnectGattActive) closeGatt()
        val scanner = bluetoothManager?.adapter?.bluetoothLeScanner
        if (scanner == null) {
            fail("Нельзя проверить уход: BLE scanner недоступен")
            return
        }
        departureMode = true
        update(State.WAITING_DEPARTURE, "Ожидание ухода от замка")
        if (departureScanActive) {
            if (!autoConnectLinkConnected) scheduleDepartureConfirmation()
            return
        }
        val settings = ScanSettings.Builder()
            /* BALANCED вместо LOW_POWER: у LOW_POWER окно сканирования ~0.5с раз в ~5с,
             * из-за чего после разрыва соединения первый новый RSSI мог появляться
             * на экране с заметной задержкой. BALANCED (~0.5с окно раз в ~2с) ловит
             * рекламные пакеты заметно быстрее и всё ещё экономичнее LOW_LATENCY,
             * который тут избыточен — здесь только следим за присутствием/уходом. */
            .setScanMode(ScanSettings.SCAN_MODE_BALANCED)
            .build()
        // Тот же фильтр нужен после разрыва, чтобы Android продолжал получать
        // рекламу при выключенном экране; UUID проверяется в scanCallback.
        val filter = ScanFilter.Builder().setDeviceName(BleProtocol.DEVICE_NAME).build()
        scanner.startScan(listOf(filter), settings, scanCallback)
        connectionScanActive = true
        departureScanActive = true
        if (!autoConnectLinkConnected) scheduleDepartureConfirmation()
    }

    override fun openNow() {
        if (autoConnectGattActive && !autoConnectLinkConnected) {
            Log.i(TAG, "OPEN_SKIPPED_AUTO_CONNECT_DISCONNECTED: waiting for system GATT reconnect")
            return
        }
        if (autoConnectGattActive &&
            (autoConnectOpenSentForPresence || autoConnectOpenWritePending)) {
            Log.i(TAG, "OPEN_SKIPPED_AUTO_CONNECT_ALREADY_SENT_OR_PENDING")
            return
        }
        val characteristic = lockService?.getCharacteristic(BleProtocol.COMMAND_UUID)
        val currentGatt = gatt
        if (state != State.READY || characteristic == null || currentGatt == null) {
            fail("Сначала подключитесь к зарегистрированному замку")
            return
        }
        pendingLockCommand = "OPEN"
        if (autoConnectGattActive) autoConnectOpenWritePending = true
        if (!write(characteristic, BleProtocol.OPEN_COMMAND, currentGatt)) {
            autoConnectOpenWritePending = false
            fail("Не удалось запустить запись OPEN")
        }
    }

    override fun forceOpenNow() {
        val characteristic = lockService?.getCharacteristic(BleProtocol.COMMAND_UUID)
        val currentGatt = gatt
        if ((state != State.READY && state != State.OPEN_SENT) ||
            characteristic == null || currentGatt == null) {
            fail("Сначала подключитесь к зарегистрированному замку")
            return
        }
        pendingLockCommand = "FORCE_OPEN"
        if (!write(characteristic, BleProtocol.FORCE_OPEN_COMMAND, currentGatt)) {
            fail("Не удалось запустить запись FORCE_OPEN")
        }
    }

    override fun toggleKeepOpen() {
        val characteristic = lockService?.getCharacteristic(BleProtocol.COMMAND_UUID)
        val currentGatt = gatt
        if (state != State.READY || characteristic == null || currentGatt == null) {
            fail("Сначала подключитесь к зарегистрированному замку")
            return
        }
        pendingLockCommand = "KEEP_OPEN"
        if (!write(characteristic, BleProtocol.KEEP_OPEN_COMMAND, currentGatt)) {
            fail("Не удалось запустить запись KEEP_OPEN")
        }
    }

    override fun setWorkingMode(alwaysOpen: Boolean, closeWhenDisablingAlwaysOpen: Boolean) {
        val characteristic = lockService?.getCharacteristic(BleProtocol.COMMAND_UUID)
        val currentGatt = gatt
        if (state != State.READY || characteristic == null || currentGatt == null) {
            fail("Сначала подключитесь к зарегистрированному замку")
            return
        }
        pendingLockCommand = when {
            alwaysOpen -> "MODE_ALWAYS"
            closeWhenDisablingAlwaysOpen -> "AUTO_OFF_CLOSE"
            else -> "MODE_AUTO"
        }
        val command = when {
            alwaysOpen -> BleProtocol.MODE_ALWAYS_COMMAND
            closeWhenDisablingAlwaysOpen -> BleProtocol.AUTO_OFF_CLOSE_COMMAND
            else -> BleProtocol.MODE_AUTO_COMMAND
        }
        if (!write(characteristic, command, currentGatt)) {
            fail("Не удалось запустить запись $pendingLockCommand")
        }
    }

    fun disableAutomaticModeAndClose(): Boolean {
        val characteristic = lockService?.getCharacteristic(BleProtocol.COMMAND_UUID)
        val currentGatt = gatt
        if ((state != State.READY && state != State.OPEN_SENT) ||
            characteristic == null || currentGatt == null) {
            return false
        }
        pendingLockCommand = "AUTO_OFF_CLOSE"
        return write(characteristic, BleProtocol.AUTO_OFF_CLOSE_COMMAND, currentGatt).also {
            if (!it) fail("Не удалось запустить запись AUTO_OFF_CLOSE")
        }
    }

    override fun readAdminConfig() {
        val characteristic = lockService?.getCharacteristic(BleProtocol.ADMIN_CONFIG_UUID)
        if (characteristic == null || !enqueueGattOperation("ADMIN_CONFIG") {
                gatt?.readCharacteristic(characteristic) == true
            }) {
            pendingConfigVerification = null
            fail("Не удалось прочитать настройки")
        }
    }

    override fun writeAdminConfig(value: String) {
        val characteristic = lockService?.getCharacteristic(BleProtocol.ADMIN_CONFIG_UUID)
        val currentGatt = gatt
        if (state != State.READY || characteristic == null || currentGatt == null) {
            fail("Сначала подключитесь к зарегистрированному замку")
            return
        }
        update(State.DISCOVERING, "Сохранение настроек: $value")
        pendingConfigVerification = value
        if (!write(characteristic, value.toByteArray(Charsets.US_ASCII), currentGatt)) {
            pendingConfigVerification = null
        }
    }

    fun readAdminState() = readAdmin(BleProtocol.ADMIN_STATE_UUID, "состояние")
    override fun retryAdminProbe() {
        adminProbePending = true
        readAdminState()
    }

    override fun readAdminSchema() {
        val characteristic = lockService?.getCharacteristic(BleProtocol.ADMIN_SCHEMA_UUID)
        val currentGatt = gatt
        if (state != State.READY || characteristic == null || currentGatt == null) {
            handler.post { listener.onAdminSchemaError("Характеристика схемы отсутствует в прошивке") }
            return
        }
        schemaChunks = null
        schemaExpectedLength = 0
        schemaExpectedCrc = 0L
        schemaExpectedCount = 0
        requestSchemaChunk(0)
    }

    private fun requestSchemaChunk(index: Int) {
        val characteristic = lockService?.getCharacteristic(BleProtocol.ADMIN_CONTROL_UUID)
        val currentGatt = gatt
        if (state != State.READY || characteristic == null || currentGatt == null) {
            schemaError("GATT-сеанс не готов для чтения схемы")
            return
        }
        schemaControlPending = true
        if (!write(characteristic, "SCHEMA:$index".toByteArray(Charsets.US_ASCII), currentGatt)) {
            schemaControlPending = false
            schemaError("Не удалось запросить фрагмент схемы")
        }
    }

    private fun readAdminSchemaChunk() {
        val characteristic = lockService?.getCharacteristic(BleProtocol.ADMIN_SCHEMA_UUID)
        if (characteristic == null || !enqueueGattOperation("ADMIN_SCHEMA") {
                gatt?.readCharacteristic(characteristic) == true
            }) {
            schemaError("Не удалось прочитать фрагмент схемы")
        }
    }

    private fun schemaError(detail: String) {
        schemaControlPending = false
        schemaChunks = null
        handler.post { listener.onAdminSchemaError(detail) }
    }
    override fun pollAdminState() {
        if (state != State.READY) return
        val characteristic = lockService?.getCharacteristic(BleProtocol.ADMIN_STATE_UUID) ?: return
        if (hasPendingGattOperation("ADMIN_STATE_POLL")) return
        adminProbePending = true
        enqueueGattOperation("ADMIN_STATE_POLL") {
            gatt?.readCharacteristic(characteristic) == true
        }
    }
    override fun readAdminBonds() = readAdmin(BleProtocol.ADMIN_BONDS_UUID, "список телефонов")
    override fun writeCurrentPhoneName() = writePhoneName()
    fun readAdminEvents() = readAdmin(BleProtocol.ADMIN_EVENTS_UUID, "журнал событий")
    override fun readOtaConfig() = readAdmin(BleProtocol.OTA_CONFIG_UUID, "Wi-Fi/OTA настройки")
    override fun readOtaStatus(): Boolean {
        val characteristic = lockService?.getCharacteristic(BleProtocol.OTA_STATUS_UUID)
        val currentGatt = gatt
        if (state != State.READY || characteristic == null || currentGatt == null) return false
        return enqueueGattOperation("OTA_STATUS") {
            currentGatt.readCharacteristic(characteristic)
        }
    }
    override fun writeOtaConfig(value: String) {
        val characteristic = lockService?.getCharacteristic(BleProtocol.OTA_CONFIG_UUID)
        val currentGatt = gatt
        if (state != State.READY || characteristic == null || currentGatt == null) {
            fail("Сначала подключитесь к зарегистрированному замку")
            return
        }
        persistentOtaConfigWritePending = value.startsWith(BleProtocol.OTA_PERSIST_PREFIX)
        if (!write(characteristic, value.toByteArray(Charsets.UTF_8), currentGatt)) {
            val persistent = persistentOtaConfigWritePending
            persistentOtaConfigWritePending = false
            fail(if (persistent) "Не удалось сохранить Wi-Fi/OTA настройки"
                 else "Не удалось передать настройки точки доступа")
        }
    }
    override fun startHomeOta() = writeAdminControl("OTA_HOME")
    fun allowRegistration() = writeAdminControl("REGISTER")
    override fun deleteBond(address: String) {
        refreshBondsAfterControl = true
        if (!writeAdminControl("DELETE:${address.trim().uppercase()}")) {
            refreshBondsAfterControl = false
        }
    }
    override fun deleteAllBonds() {
        refreshBondsAfterControl = true
        if (!writeAdminControl("DELETE_ALL")) refreshBondsAfterControl = false
    }
    override fun rebootEsp32() = writeAdminControl("REBOOT")

    private fun readAdmin(uuid: java.util.UUID, title: String) {
        val characteristic = lockService?.getCharacteristic(uuid)
        if (state != State.READY || characteristic == null ||
            !enqueueGattOperation(title) { gatt?.readCharacteristic(characteristic) == true }) {
            fail("Не удалось прочитать $title")
        }
    }

    private fun writeAdminControl(command: String): Boolean {
        val characteristic = lockService?.getCharacteristic(BleProtocol.ADMIN_CONTROL_UUID)
        val currentGatt = gatt
        if (state != State.READY || characteristic == null || currentGatt == null) {
            fail("Сначала подключитесь к зарегистрированному замку")
            return false
        }
        return write(characteristic, command.toByteArray(Charsets.US_ASCII), currentGatt)
    }

    private fun writePhoneName() {
        val characteristic = lockService?.getCharacteristic(BleProtocol.PHONE_NAME_UUID)
        val currentGatt = gatt
        if (characteristic == null || currentGatt == null) {
            readAdminBonds()
            return
        }
        val source = bluetoothManager?.adapter?.name?.trim().orEmpty().ifBlank {
            "${Build.MANUFACTURER} ${Build.MODEL}".trim()
        }
        val builder = StringBuilder()
        source.codePoints().forEach { codePoint ->
            val candidate = builder.toString() + String(Character.toChars(codePoint))
            if (candidate.toByteArray(Charsets.UTF_8).size <= PHONE_NAME_MAX_BYTES) {
                builder.appendCodePoint(codePoint)
            }
        }
        val name = builder.toString().ifBlank { "Android" }
        if (!write(characteristic, name.toByteArray(Charsets.UTF_8), currentGatt)) {
            readAdminBonds()
        }
    }

    private fun write(
        characteristic: BluetoothGattCharacteristic,
        value: ByteArray,
        currentGatt: BluetoothGatt
    ): Boolean {
        return enqueueGattOperation("WRITE:${characteristic.uuid}") {
            if (Build.VERSION.SDK_INT >= 33) {
                currentGatt.writeCharacteristic(
                    characteristic, value, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                ) == BluetoothStatusCodes.SUCCESS
            } else {
                @Suppress("DEPRECATION")
                characteristic.value = value
                @Suppress("DEPRECATION")
                currentGatt.writeCharacteristic(characteristic)
            }
        }
    }

    override fun close() {
        stopRssiPolling()
        stopSignalMonitoring()
        stopScan()
        closeGatt()
        runCatching { context.unregisterReceiver(bondReceiver) }
    }

    private val departureConfirmed = Runnable {
        if (!departureMode) return@Runnable
        stopScan()
        departureMode = false
        autoConnectOpenSentForPresence = false
        advertisementTonePlayedForPresence = false
        update(State.DEPARTED, "Уход подтверждён, новый цикл разрешён")
    }

    private fun playAdvertisementToneOnceForPresence() {
        if (advertisementTonePlayedForPresence) return
        advertisementTonePlayedForPresence = true
        Log.i(TAG, "BLE lock presence confirmed; playing advertisement tone")
        BleEventTones.advertisementDetected(context)
    }

    private val rssiPoll = Runnable {
        val currentGatt = gatt
        if (currentGatt != null) {
            if (state == State.READY && activeGattOperation == null) {
                if (!enqueueGattOperation("RSSI") {
                        currentGatt.readRemoteRssi()
                    }) scheduleRssiPoll()
            } else {
                scheduleRssiPoll()
            }
        }
    }

    private fun scheduleRssiPoll() {
        handler.removeCallbacks(rssiPoll)
        if (gatt != null) handler.postDelayed(rssiPoll, RSSI_POLL_INTERVAL_MS)
    }

    fun requestImmediateRssi() {
        handler.removeCallbacks(rssiPoll)
        handler.post(rssiPoll)
    }

    private fun stopRssiPolling() {
        handler.removeCallbacks(rssiPoll)
        handler.post { listener.onSignalRssi(null) }
    }

    private fun stopScan() {
        handler.removeCallbacks(departureConfirmed)
        handler.removeCallbacks(connectionScanWatchdog)
        pendingConnectionScan?.let(handler::removeCallbacks)
        pendingConnectionScan = null
        connectionScanActive = false
        runCatching { bluetoothManager?.adapter?.bluetoothLeScanner?.stopScan(scanCallback) }
        departureScanActive = false
    }

    private fun connect(device: BluetoothDevice, autoConnect: Boolean = false) {
        if (gatt != null) {
            Log.i(TAG, "ACTIVE_CONNECT_SKIPPED_ALREADY_RUNNING: GATT request already exists")
            return
        }
        setAdminAvailability(false)
        update(
            State.CONNECTING,
            if (autoConnect) "Ожидание системного BLE-подключения к ${device.address}"
            else "Подключение к ${device.address}"
        )
        val newGatt = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            device.connectGatt(context, autoConnect, gattCallback, BluetoothDevice.TRANSPORT_LE)
        } else {
            device.connectGatt(context, autoConnect, gattCallback)
        }
        gatt = newGatt
        if (autoConnect) {
            autoConnectGattActive = true
            autoConnectLinkConnected = false
            autoConnectOpenWritePending = false
            autoConnectOpenSentForPresence = false
            secureStatusProbeAttempts = 0
            Log.i(TAG, "EXPERIMENT_AUTO_CONNECT_PENDING: gatt=${newGatt != null}")
            if (newGatt == null) {
                closeGatt()
                fail("Не удалось запустить системное BLE-подключение")
                return
            }
            armNativeAutoConnectWatchdog(newGatt)
            return
        }
        val watchdog = Runnable {
            if (gatt !== newGatt || state != State.CONNECTING) return@Runnable
            Log.w(TAG, "connectGatt callback timeout; recreating GATT")
            closeGatt()
            fail("Таймаут подключения BLE")
        }
        connectWatchdog = watchdog
        handler.postDelayed(watchdog, GATT_CONNECT_TIMEOUT_MS)
    }

    private fun armNativeAutoConnectWatchdog(request: BluetoothGatt) {
        connectWatchdog?.let(handler::removeCallbacks)
        val watchdog = Runnable {
            if (gatt !== request || !autoConnectGattActive || autoConnectLinkConnected ||
                autoConnectOpenSentForPresence) return@Runnable
            Log.w(TAG, "NATIVE_AUTO_CONNECT_STALLED: trying direct GATT connection")
            val device = request.device
            closeGatt()
            connect(device)
        }
        connectWatchdog = watchdog
        handler.postDelayed(watchdog, NATIVE_AUTO_CONNECT_TIMEOUT_MS)
    }

    private fun pairOrConnect(device: BluetoothDevice) {
        when (device.bondState) {
            BluetoothDevice.BOND_BONDED -> connect(device)
            BluetoothDevice.BOND_BONDING -> {
                pendingBondDevice = device
                update(State.BONDING, "Ожидание системного Pairing/Bonding")
            }
            else -> {
                pendingBondDevice = device
                update(State.BONDING, "Запуск системного Pairing/Bonding")
                if (!device.createBond()) {
                    pendingBondDevice = null
                    fail("Не удалось запустить системный Pairing/Bonding")
                }
            }
        }
    }

    private fun readInfo() {
        val characteristic = lockService?.getCharacteristic(BleProtocol.INFO_UUID)
        if (characteristic == null || !enqueueGattOperation("LOCK_INFO") {
                val started = gatt?.readCharacteristic(characteristic) == true
                Log.d(TAG, "start GATT LOCK_INFO read=$started")
                started
            }) {
            fail("Не удалось прочитать LOCK_INFO")
        } else {
            update(State.DISCOVERING, "Чтение LOCK_INFO")
        }
    }

    private fun readStatus() {
        val characteristic = lockService?.getCharacteristic(BleProtocol.STATUS_UUID)
        if (characteristic == null || !enqueueGattOperation("LOCK_STATUS") {
                gatt?.readCharacteristic(characteristic) == true
            }) {
            if (autoConnectGattActive && controlOnly) {
                scheduleSecureStatusProbeRetry("queue rejected")
            } else {
                fail("Не удалось прочитать LOCK_STATUS")
            }
        }
    }

    private fun scheduleSecureStatusProbeRetry(reason: String) {
        handler.post {
            val candidate = gatt ?: return@post
            if (!autoConnectGattActive || !controlOnly) return@post
            secureStatusProbeRetry?.let(handler::removeCallbacks)
            secureStatusProbeAttempts++
            val delay = (SECURE_STATUS_PROBE_RETRY_MS shl
                (secureStatusProbeAttempts - 1).coerceAtMost(3)).coerceAtMost(
                SECURE_STATUS_PROBE_MAX_RETRY_MS
            )
            Log.w(
                TAG,
                "AUTO_CONNECT_SECURE_PROBE_RETRY: reason=$reason attempt=$secureStatusProbeAttempts delay=${delay}ms"
            )
            val retry = Runnable {
                secureStatusProbeRetry = null
                if (isGattCallbackCurrent(candidate) && lockService != null) {
                    readStatus()
                }
            }
            secureStatusProbeRetry = retry
            handler.postDelayed(retry, delay)
        }
    }

    private fun handleRead(uuid: java.util.UUID, value: ByteArray, statusCode: Int) {
        Log.d(TAG, "handleRead uuid=$uuid status=$statusCode")
        if (statusCode != BluetoothGatt.GATT_SUCCESS) {
            if (uuid == BleProtocol.ADMIN_STATE_UUID && adminProbePending) {
                adminProbePending = false
                setAdminAvailability(false)
                update(State.READY, "Обычный режим: административные функции недоступны")
                return
            }
            if (uuid == BleProtocol.ADMIN_SCHEMA_UUID) {
                listener.onAdminSchemaError("Прошивка отклонила чтение схемы, GATT status: $statusCode")
                return
            }
            if (uuid == BleProtocol.STATUS_UUID && autoConnectGattActive && controlOnly) {
                scheduleSecureStatusProbeRetry("read status=$statusCode")
                return
            }
            fail("Защищённое чтение отклонено, GATT status: $statusCode")
            return
        }
        val text = value.toString(Charsets.UTF_8)
        /* GATT-колбэки приходят на фоновом (Binder) потоке, т.к. connectGatt()
         * вызывается без явного Handler на главный Looper. Слушатели ниже трогают
         * View (EditText, Spinner и т.д.), поэтому обязательно нужно перейти на
         * главный поток — иначе, например, Spinner.setSelection() тихо не
         * применяется (в отличие от EditText.setText(), который может "проскочить"
         * без явной ошибки). */
        handler.post {
            when (uuid) {
                BleProtocol.INFO_UUID -> {
                    info = text
                    listener.onLockData(info, status)
                    // The firmware stores a human-readable name separately
                    // from the NimBLE bond. Do this on every handshake; the
                    // ESP-side save-once guard avoids unnecessary NVS writes.
                    handler.post { writePhoneName() }
                    if (controlOnly) {
                        update(State.READY, "Замок готов к автоматическому управлению")
                        scheduleRssiPoll()
                    } else {
                        readStatus()
                    }
                }
                BleProtocol.STATUS_UUID -> {
                    status = text
                    listener.onLockData(info, status)
                    if (autoConnectGattActive && controlOnly) {
                        secureStatusProbeRetry?.let(handler::removeCallbacks)
                        secureStatusProbeRetry = null
                        secureStatusProbeAttempts = 0
                        Log.i(TAG, "AUTH_SUCCESS: encrypted lock status read succeeded")
                    }
                    update(State.READY, "Замок готов к защищённой команде")
                    scheduleRssiPoll()
                    if (!controlOnly) {
                        val currentGatt = gatt
                        postReadyMtuPending = currentGatt != null &&
                            currentGatt.requestMtu(REQUESTED_MTU)
                        if (!postReadyMtuPending) {
                            adminProbePending = true
                            handler.post { readAdminState() }
                        }
                    }
                }
                BleProtocol.ADMIN_CONFIG_UUID -> {
                    listener.onAdminConfig(text)
                    val expected = pendingConfigVerification
                    pendingConfigVerification = null
                    when {
                        expected == null ->
                            update(State.READY, "Настройки замка прочитаны")
                        adminConfigMatches(expected, text) ->
                            update(State.READY, "Настройки сохранены и подтверждены чтением")
                        else ->
                            update(
                                State.READY,
                                "ВНИМАНИЕ: замок вернул другие значения после сохранения " +
                                    "(записано \"$expected\", считано \"$text\") — " +
                                    "повторите сохранение"
                            )
                    }
                }
                BleProtocol.ADMIN_SCHEMA_UUID -> handleSchemaChunk(text)
                BleProtocol.ADMIN_STATE_UUID -> {
                    listener.onAdminState(text)
                    if (adminProbePending) {
                        adminProbePending = false
                        setAdminAvailability(true)
                    }
                }
                BleProtocol.ADMIN_BONDS_UUID -> listener.onAdminBonds(text)
                BleProtocol.ADMIN_EVENTS_UUID -> listener.onAdminEvents(text)
                BleProtocol.OTA_CONFIG_UUID -> listener.onOtaConfig(text)
                BleProtocol.OTA_STATUS_UUID -> listener.onOtaStatus(text)
            }
        }
    }

    private fun adminConfigMatches(expected: String, actual: String): Boolean {
        if (expected == actual) return true
        val expectedFields = expected.split(',')
        val actualFields = actual.split(',')
        // Older firmware omits the optional advertising-power field from the
        // read response. The seven persisted values are still fully verified.
        return actualFields.size == 7 && expectedFields.size >= 7 &&
            actualFields == expectedFields.take(7)
    }

    private fun handleSchemaChunk(frame: String) {
        val separator = frame.indexOf('\n')
        if (separator <= 0) {
            schemaError("Некорректный заголовок схемы")
            return
        }
        val header = frame.substring(0, separator).split(';')
            .mapNotNull { part -> part.indexOf('=').takeIf { it > 0 }?.let { part.substring(0, it) to part.substring(it + 1) } }
            .toMap()
        val index = header["index"]?.toIntOrNull()
        val count = header["count"]?.toIntOrNull()
        val length = header["length"]?.toIntOrNull()
        val crc = header["crc32"]?.toLongOrNull(16)
        val encoded = frame.substring(separator + 1)
        if (header["schema"] != "1" || index == null || count == null || length == null ||
            crc == null || index !in 0 until count || count !in 1..32 ||
            length !in 1..DeclarativeUiSchema.MAX_BYTES) {
            schemaError("Некорректные параметры фрагмента схемы")
            return
        }
        val chunk = runCatching { Base64.decode(encoded, Base64.DEFAULT) }.getOrElse {
            schemaError("Некорректное кодирование фрагмента схемы")
            return
        }
        if (schemaChunks == null) {
            schemaChunks = arrayOfNulls(count)
            schemaExpectedCount = count
            schemaExpectedLength = length
            schemaExpectedCrc = crc
        }
        if (count != schemaExpectedCount || length != schemaExpectedLength || crc != schemaExpectedCrc) {
            schemaError("Фрагменты схемы имеют разные параметры")
            return
        }
        schemaChunks?.set(index, chunk)
        if (schemaChunks?.all { it != null } != true) {
            requestSchemaChunk(index + 1)
            return
        }
        val bytes = ByteArray(schemaExpectedLength)
        var offset = 0
        schemaChunks!!.forEach { part ->
            val value = part ?: return@forEach
            if (offset + value.size > bytes.size) {
                schemaError("Размер схемы не совпадает с заголовком")
                return
            }
            value.copyInto(bytes, offset)
            offset += value.size
        }
        if (offset != bytes.size) {
            schemaError("Схема передана не полностью")
            return
        }
        val checksum = CRC32().apply { update(bytes) }.value
        if (checksum != schemaExpectedCrc) {
            schemaError("CRC-32 схемы не совпал")
            return
        }
        schemaChunks = null
        listener.onAdminSchema(bytes.toString(Charsets.UTF_8))
    }

    private fun update(newState: State, detail: String) {
        Log.d(TAG, "state -> $newState ($detail)")
        state = newState
        handler.post { listener.onBleState(newState, detail) }
        // Частые отдельные RSSI scan после READY/OPEN_SENT приводят к
        // SCAN_FAILED_SCANNING_TOO_FREQUENTLY на HyperOS. Значение для UI
        // уже обновляется существующим GATT-опросом раз в 2 секунды.
        if (signalMonitoring) {
            stopSignalMonitoring()
        }
    }

    private fun playDisconnectToneIfConnected() {
        if (!gattConnectionEstablished) return
        gattConnectionEstablished = false
        Log.i(TAG, "GATT_DISCONNECTED")
    }

    private fun setAdminAvailability(available: Boolean) {
        handler.post { listener.onAdminAvailability(available) }
    }

    private fun fail(message: String) = update(State.ERROR, message)

    private fun closeGatt() {
        autoConnectGattActive = false
        autoConnectLinkConnected = false
        autoConnectOpenWritePending = false
        autoConnectOpenSentForPresence = false
        secureStatusProbeRetry?.let(handler::removeCallbacks)
        secureStatusProbeRetry = null
        secureStatusProbeAttempts = 0
        playDisconnectToneIfConnected()
        connectWatchdog?.let(handler::removeCallbacks)
        connectWatchdog = null
        stopRssiPolling()
        finishGattOperation(GattOperationResult.CANCELLED, pumpNext = false)
        activeGattTimeout?.let(handler::removeCallbacks)
        activeGattTimeout = null
        gattQueue.clear()
        activeGattOperation = null
        mtuHandshakeComplete = false
        mtuNegotiationRetries = 0
        postReadyMtuPending = false
        mtuRetryWatchdog?.let(handler::removeCallbacks)
        mtuRetryWatchdog = null
        adminProbePending = false
        pendingConfigVerification = null
        lockService = null
        runCatching { gatt?.disconnect() }
        gatt?.close()
        gatt = null
    }

    private fun handleAutoConnectDisconnect(statusCode: Int) {
        autoConnectLinkConnected = false
        autoConnectOpenWritePending = false
        secureStatusProbeRetry?.let(handler::removeCallbacks)
        secureStatusProbeRetry = null
        secureStatusProbeAttempts = 0
        playDisconnectToneIfConnected()
        handler.post { listener.onConnectionLost() }
        stopRssiPolling()
        stopSignalMonitoring()
        setAdminAvailability(false)
        finishGattOperation(GattOperationResult.CANCELLED, pumpNext = false)
        activeGattTimeout?.let(handler::removeCallbacks)
        activeGattTimeout = null
        gattQueue.clear()
        activeGattOperation = null
        mtuHandshakeComplete = false
        mtuNegotiationRetries = 0
        postReadyMtuPending = false
        mtuRetryWatchdog?.let(handler::removeCallbacks)
        mtuRetryWatchdog = null
        adminProbePending = false
        pendingConfigVerification = null
        lockService = null
        Log.i(TAG, "NATIVE_AUTO_CONNECT_RETAINED: status=$statusCode; keeping GATT request armed")
        if (autoConnectOpenSentForPresence) {
            waitForDeparture()
        } else {
            update(State.CONNECTING, "Ожидание системного GATT-переподключения")
            gatt?.let(::armNativeAutoConnectWatchdog)
        }
    }

    private fun isGattCallbackCurrent(candidate: BluetoothGatt): Boolean =
        this.gatt === candidate && (!autoConnectGattActive || autoConnectLinkConnected)

    private fun scheduleDepartureConfirmation() {
        handler.removeCallbacks(departureConfirmed)
        handler.postDelayed(departureConfirmed, DEPARTURE_SILENCE_MS)
    }

    companion object {
        private const val TAG = "BleLock"
        private var lastConnectionScanStartMs = 0L
        private const val SCAN_START_INTERVAL_MS = 15_000L
        private const val SCAN_SILENCE_RETRY_MS = 60_000L
        private const val SCAN_THROTTLE_RETRY_MS = 60_000L
        private const val DEPARTURE_SILENCE_MS = 20_000L
        private const val PHONE_NAME_MAX_BYTES = 20
        private const val RSSI_POLL_INTERVAL_MS = 2_000L
        private const val GATT_OPERATION_TIMEOUT_MS = 10_000L
        private const val INITIAL_SECURE_READ_TIMEOUT_MS = 3_000L
        private const val GATT_CONNECT_TIMEOUT_MS = 8_000L
        private const val NATIVE_AUTO_CONNECT_TIMEOUT_MS = 90_000L
        /* Совпадает с CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=256 в прошивке ESP32. */
        private const val REQUESTED_MTU = 256
        private const val MTU_FALLBACK_DELAY_MS = 500L
        private const val MAX_MTU_NEGOTIATION_RETRIES = 2
        private const val MTU_RETRY_DELAY_MS = 400L
        private const val MTU_RETRY_CALLBACK_TIMEOUT_MS = 1_500L
        private const val SECURITY_SETTLE_DELAY_MS = 700L
        private const val SECURE_STATUS_PROBE_RETRY_MS = 750L
        private const val SECURE_STATUS_PROBE_MAX_RETRY_MS = 4_000L
    }
}
