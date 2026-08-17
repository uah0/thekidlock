package com.blelock.app

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattService
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
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
import android.util.Log
import android.os.Looper
import android.os.SystemClock

@SuppressLint("MissingPermission")
class BleRepository(
    private val context: Context,
    private val listener: Listener,
    private val controlOnly: Boolean = false
) {
    interface Listener {
        fun onBleState(state: State, detail: String)
        fun onConnectionLost() {}
        fun onLockData(info: String?, status: String?)
        fun onSignalRssi(rssi: Int?) {}
        fun onAdminConfig(value: String) {}
        fun onAdminAvailability(available: Boolean) {}
        fun onAdminState(value: String) {}
        fun onAdminBonds(value: String) {}
        fun onAdminEvents(value: String) {}
    }

    enum class State {
        IDLE, SCANNING, CONNECTING, BONDING, DISCOVERING, READY, OPEN_SENT,
        WAITING_DEPARTURE, DEPARTED, ERROR
    }

    private val handler = Handler(Looper.getMainLooper())
    private val bluetoothManager = context.getSystemService(BluetoothManager::class.java)
    private val lockSettings = LockSettings(context)
    private var gatt: BluetoothGatt? = null
    private var lockService: BluetoothGattService? = null
    private var state = State.IDLE
    private var info: String? = null
    private var status: String? = null

    /** Текущее состояние подключения, доступное снаружи без изменения его. */
    val currentState: State get() = state

    /** true, если уже идёт поиск/подключение/соединение установлено — новый findAndConnect() не нужен.
     *  DEPARTED сюда не входит: это завершённое состояние («уход подтверждён»),
     *  которое как раз ждёт вызова findAndConnect() для нового цикла поиска. */
    fun isConnectingOrConnected(): Boolean =
        state != State.IDLE && state != State.ERROR && state != State.DEPARTED
    private var departureMode = false
    private var adminProbePending = false
    private var signalMonitoring = false
    private var refreshBondsAfterControl = false
    /* Строка, только что отправленная в CHR_ADMIN_CONFIG, ожидающая подтверждения
     * чтением. null, когда подтверждать нечего (обычное чтение по кнопке). */
    private var pendingConfigVerification: String? = null
    private var pendingBondDevice: BluetoothDevice? = null
    private var weakRssiStreak = 0

    private var lastWeakSignalLogAt = 0L

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            if (departureMode) {
                handler.removeCallbacks(departureConfirmed)
                handler.postDelayed(departureConfirmed, DEPARTURE_SILENCE_MS)
                handler.post { listener.onSignalRssi(result.rssi) }
                if (result.rssi >= lockSettings.connectRssiThreshold) {
                    // Сигнал восстановился раньше, чем истекла тишина: ухода не было,
                    // выходим из departureMode и переподключаемся сразу, а не ждём таймаут.
                    departureMode = false
                    stopScan()
                    lockSettings.lockAddress = result.device.address
                    pairOrConnect(result.device)
                }
                return
            }
            val threshold = lockSettings.connectRssiThreshold
            if (result.rssi < threshold) {
                handler.post { listener.onSignalRssi(result.rssi) }
                val now = SystemClock.elapsedRealtime()
                if (now - lastWeakSignalLogAt > WEAK_SIGNAL_LOG_INTERVAL_MS) {
                    lastWeakSignalLogAt = now
                    update(State.SCANNING, "Замок найден, но сигнал слабый: ${result.rssi} дБм (нужно ≥ $threshold)")
                }
                return
            }
            stopScan()
            lockSettings.lockAddress = result.device.address
            pairOrConnect(result.device)
        }

        override fun onScanFailed(errorCode: Int) {
            fail("Ошибка BLE scan: $errorCode")
        }
    }

    private val signalScanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            // Пассивный скан — только для живого отображения RSSI. Он присылает
            // замеры пачками (рекламные пакеты раз в 100-1000мс), и обычное
            // дрожание RSSI внутри такой пачки может набрать WEAK_RSSI_DISCONNECT_STREAK
            // подряд слабых замеров за доли секунды — решение об отключении
            // намеренно принимает только evaluateConnectedRssi() из GATT-опроса
            // (onReadRemoteRssi), у которого фиксированный шаг RSSI_POLL_INTERVAL_MS.
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
            Log.d(TAG, "onConnectionStateChange gattHash=${System.identityHashCode(gatt)} status=$statusCode newState=$newState bondState=${gatt.device.bondState}")
            if (statusCode != BluetoothGatt.GATT_SUCCESS) {
                handler.post { listener.onConnectionLost() }
                fail("Ошибка GATT connection: $statusCode")
                closeGatt()
                return
            }
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    update(State.DISCOVERING, "Подключено, поиск GATT-сервиса")
                    gatt.discoverServices()
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
                    handler.post { listener.onConnectionLost() }
                    stopRssiPolling()
                    setAdminAvailability(false)
                    update(State.IDLE, "Замок отключён")
                    closeGatt()
                }
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, statusCode: Int) {
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
            /* По умолчанию ATT MTU = 23 байта (~20 байт полезной нагрузки на запись).
             * Строка admin-конфигурации (rssi, таймеры, gpio, мощность маяка) длиннее
             * этого лимита и будет молча обрезана без ошибки на некоторых стеках, если
             * не договориться о большем MTU. Прошивка поддерживает до 256 байт
             * (CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=256). Инициатором обмена MTU по
             * спецификации BLE должен быть центральное устройство (телефон). */
            if (!gatt.requestMtu(REQUESTED_MTU)) {
                Log.w(TAG, "requestMtu не удалось инициировать, продолжаем с MTU по умолчанию")
                proceedAfterMtuNegotiation(gatt)
            }
        }

        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, statusCode: Int) {
            Log.d(TAG, "onMtuChanged mtu=$mtu status=$statusCode")
            proceedAfterMtuNegotiation(gatt)
        }

        @Deprecated("Used for Android 8-12 callback compatibility")
        override fun onCharacteristicRead(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            statusCode: Int
        ) {
            handleRead(characteristic.uuid, characteristic.value ?: byteArrayOf(), statusCode)
        }

        override fun onCharacteristicRead(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
            statusCode: Int
        ) {
            handleRead(characteristic.uuid, value, statusCode)
        }

        override fun onCharacteristicWrite(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            statusCode: Int
        ) {
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
                if (statusCode == BluetoothGatt.GATT_SUCCESS) {
                    update(State.READY, "Команда управления принята замком")
                    if (refreshBondsAfterControl) handler.post { readAdminBonds() }
                } else {
                    fail("Команда управления отклонена, GATT status: $statusCode")
                }
                refreshBondsAfterControl = false
                return
            }
            if (characteristic.uuid == BleProtocol.PHONE_NAME_UUID) {
                if (statusCode == BluetoothGatt.GATT_SUCCESS) {
                    update(State.READY, "Имя телефона сохранено")
                } else {
                    update(State.READY, "Имя телефона не сохранено, GATT status: $statusCode")
                }
                handler.post { readAdminBonds() }
                return
            }
            if (characteristic.uuid != BleProtocol.COMMAND_UUID) return
            if (statusCode == BluetoothGatt.GATT_SUCCESS) {
                update(State.OPEN_SENT, "Команда OPEN передана замку")
                handler.postDelayed({ readStatus() }, 500)
            } else {
                fail("ESP32 отклонила OPEN, GATT status: $statusCode")
            }
        }

        override fun onReadRemoteRssi(gatt: BluetoothGatt, rssi: Int, statusCode: Int) {
            val validRssi = if (statusCode == BluetoothGatt.GATT_SUCCESS) rssi else null
            handler.post { listener.onSignalRssi(validRssi) }
            if (validRssi != null && evaluateConnectedRssi(validRssi)) return
            // GATT-опрос остаётся резервным источником: если фактически все 3 BLE-слота
            // замка заняты, advertising приостанавливается (см. прошивку) и пассивный
            // скан временно перестаёт что-либо ловить — тогда опрос подхватывает RSSI.
            scheduleRssiPoll()
        }
    }

    /** Проверка «слабый сигнал» — только для GATT-опроса (onReadRemoteRssi), у которого
     *  фиксированный шаг RSSI_POLL_INTERVAL_MS. Пассивный скан сюда не попадает: он
     *  сэмплирует слишком часто и неравномерно, чтобы «3 подряд» было надёжным сигналом
     *  устойчивого ухудшения, а не мгновенного дребезга. Возвращает true, если сигнал
     *  оказался настолько слабым, что соединение уже разорвано — вызывающая сторона
     *  должна прекратить дальнейшую обработку этого замера. */
    private fun evaluateConnectedRssi(rssi: Int): Boolean {
        if (state != State.READY) return false
        if (rssi < lockSettings.disconnectRssiThreshold) {
            weakRssiStreak++
            if (weakRssiStreak >= WEAK_RSSI_DISCONNECT_STREAK) {
                disconnectDueToWeakSignal(rssi)
                return true
            }
        } else {
            weakRssiStreak = 0
        }
        return false
    }

    private fun disconnectDueToWeakSignal(rssi: Int) {
        weakRssiStreak = 0
        closeGatt()
        update(
            State.IDLE,
            "Сигнал ослаб ($rssi дБм < порога ${lockSettings.disconnectRssiThreshold} дБм), отключение"
        )
        handler.post { listener.onConnectionLost() }
    }

    private fun proceedAfterMtuNegotiation(gatt: BluetoothGatt) {
        if (gatt.device.bondState != BluetoothDevice.BOND_BONDED) {
            update(State.BONDING, "Запуск системного Pairing/Bonding")
            if (!gatt.device.createBond()) {
                fail("Не удалось запустить системный Pairing/Bonding")
            }
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

    fun findAndConnect() {
        closeGatt()
        departureMode = false
        val savedAddress = lockSettings.lockAddress
        val scanner = bluetoothManager?.adapter?.bluetoothLeScanner
        if (scanner == null) {
            fail("Bluetooth выключен или BLE scanner недоступен")
            return
        }
        update(State.SCANNING, "Поиск BLE Lock…")
        val filter = if (savedAddress != null) {
            ScanFilter.Builder().setDeviceAddress(savedAddress).build()
        } else {
            ScanFilter.Builder().setServiceUuid(BleProtocol.SERVICE_PARCEL_UUID).build()
        }
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()
        scanner.startScan(listOf(filter), settings, scanCallback)
        handler.removeCallbacks(scanTimeout)
        handler.postDelayed(scanTimeout, SCAN_TIMEOUT_MS)
    }

    fun startSignalMonitoring() {
        if (signalMonitoring) return
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
        closeGatt()
        val scanner = bluetoothManager?.adapter?.bluetoothLeScanner
        if (scanner == null) {
            fail("Нельзя проверить уход: BLE scanner недоступен")
            return
        }
        departureMode = true
        update(State.WAITING_DEPARTURE, "Ожидание ухода от замка")
        val filter = ScanFilter.Builder().setServiceUuid(BleProtocol.SERVICE_PARCEL_UUID).build()
        val settings = ScanSettings.Builder()
            /* BALANCED вместо LOW_POWER: у LOW_POWER окно сканирования ~0.5с раз в ~5с,
             * из-за чего после разрыва соединения первый новый RSSI мог появляться
             * на экране с заметной задержкой. BALANCED (~0.5с окно раз в ~2с) ловит
             * рекламные пакеты заметно быстрее и всё ещё экономичнее LOW_LATENCY,
             * который тут избыточен — здесь только следим за присутствием/уходом. */
            .setScanMode(ScanSettings.SCAN_MODE_BALANCED)
            .build()
        scanner.startScan(listOf(filter), settings, scanCallback)
        handler.removeCallbacks(departureConfirmed)
        handler.postDelayed(departureConfirmed, DEPARTURE_SILENCE_MS)
    }

    fun openNow() {
        val characteristic = lockService?.getCharacteristic(BleProtocol.COMMAND_UUID)
        val currentGatt = gatt
        if (state != State.READY || characteristic == null || currentGatt == null) {
            fail("Сначала подключитесь к зарегистрированному замку")
            return
        }
        val started = if (Build.VERSION.SDK_INT >= 33) {
            currentGatt.writeCharacteristic(
                characteristic,
                BleProtocol.OPEN_COMMAND,
                BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            ) == BluetoothGatt.GATT_SUCCESS
        } else {
            @Suppress("DEPRECATION")
            characteristic.value = BleProtocol.OPEN_COMMAND
            @Suppress("DEPRECATION")
            currentGatt.writeCharacteristic(characteristic)
        }
        if (!started) fail("Не удалось запустить запись OPEN")
    }

    fun readAdminConfig() {
        val characteristic = lockService?.getCharacteristic(BleProtocol.ADMIN_CONFIG_UUID)
        if (characteristic == null || gatt?.readCharacteristic(characteristic) != true) {
            pendingConfigVerification = null
            fail("Не удалось прочитать настройки")
        }
    }

    fun writeAdminConfig(value: String) {
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
    fun pollAdminState() {
        if (state != State.READY) return
        val characteristic = lockService?.getCharacteristic(BleProtocol.ADMIN_STATE_UUID) ?: return
        gatt?.readCharacteristic(characteristic)
    }
    fun readAdminBonds() = readAdmin(BleProtocol.ADMIN_BONDS_UUID, "список телефонов")
    fun readAdminEvents() = readAdmin(BleProtocol.ADMIN_EVENTS_UUID, "журнал событий")
    fun allowRegistration() = writeAdminControl("REGISTER")
    fun deleteBond(address: String) {
        refreshBondsAfterControl = true
        if (!writeAdminControl("DELETE:${address.trim().uppercase()}")) {
            refreshBondsAfterControl = false
        }
    }
    fun deleteAllBonds() {
        refreshBondsAfterControl = true
        if (!writeAdminControl("DELETE_ALL")) refreshBondsAfterControl = false
    }
    fun rebootEsp32() = writeAdminControl("REBOOT")

    private fun readAdmin(uuid: java.util.UUID, title: String) {
        val characteristic = lockService?.getCharacteristic(uuid)
        if (state != State.READY || characteristic == null ||
            gatt?.readCharacteristic(characteristic) != true) {
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
        val started = if (Build.VERSION.SDK_INT >= 33) {
            currentGatt.writeCharacteristic(
                characteristic, value, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            ) == BluetoothGatt.GATT_SUCCESS
        } else {
            @Suppress("DEPRECATION")
            characteristic.value = value
            @Suppress("DEPRECATION")
            currentGatt.writeCharacteristic(characteristic)
        }
        if (!started) fail("Не удалось записать BLE-характеристику")
        return started
    }

    fun close() {
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
        update(State.DEPARTED, "Уход подтверждён, новый цикл разрешён")
    }

    private val scanTimeout = Runnable {
        stopScan()
        fail("BLE Lock не найден за 15 секунд")
    }

    private val rssiPoll = Runnable {
        val currentGatt = gatt
        if (currentGatt != null) {
            if (state == State.READY) {
                if (!currentGatt.readRemoteRssi()) scheduleRssiPoll()
            } else {
                scheduleRssiPoll()
            }
        }
    }

    private fun scheduleRssiPoll() {
        handler.removeCallbacks(rssiPoll)
        if (gatt != null) handler.postDelayed(rssiPoll, RSSI_POLL_INTERVAL_MS)
    }

    private fun stopRssiPolling() {
        handler.removeCallbacks(rssiPoll)
        handler.post { listener.onSignalRssi(null) }
    }

    private fun stopScan() {
        handler.removeCallbacks(scanTimeout)
        handler.removeCallbacks(departureConfirmed)
        runCatching { bluetoothManager?.adapter?.bluetoothLeScanner?.stopScan(scanCallback) }
    }

    private fun connect(device: BluetoothDevice) {
        weakRssiStreak = 0
        setAdminAvailability(false)
        update(State.CONNECTING, "Подключение к ${device.address}")
        gatt = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
        } else {
            device.connectGatt(context, false, gattCallback)
        }
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
        if (characteristic == null || gatt?.readCharacteristic(characteristic) != true) {
            fail("Не удалось прочитать LOCK_INFO")
        } else {
            update(State.DISCOVERING, "Чтение LOCK_INFO")
        }
    }

    private fun readStatus() {
        val characteristic = lockService?.getCharacteristic(BleProtocol.STATUS_UUID)
        if (characteristic == null || gatt?.readCharacteristic(characteristic) != true) {
            fail("Не удалось прочитать LOCK_STATUS")
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
                    update(State.READY, "Замок готов к защищённой команде")
                    scheduleRssiPoll()
                    adminProbePending = true
                    handler.post { readAdminState() }
                }
                BleProtocol.ADMIN_CONFIG_UUID -> {
                    listener.onAdminConfig(text)
                    val expected = pendingConfigVerification
                    pendingConfigVerification = null
                    when {
                        expected == null ->
                            update(State.READY, "Настройки замка прочитаны")
                        text == expected ->
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
                BleProtocol.ADMIN_STATE_UUID -> {
                    listener.onAdminState(text)
                    if (adminProbePending) {
                        adminProbePending = false
                        setAdminAvailability(true)
                        handler.post { writePhoneName() }
                    }
                }
                BleProtocol.ADMIN_BONDS_UUID -> listener.onAdminBonds(text)
                BleProtocol.ADMIN_EVENTS_UUID -> listener.onAdminEvents(text)
            }
        }
    }

    private fun update(newState: State, detail: String) {
        Log.d(TAG, "state -> $newState ($detail)")
        state = newState
        handler.post { listener.onBleState(newState, detail) }
        if (newState == State.READY) {
            // Пока замок READY, прошивка почти всегда продолжает advertising
            // (см. ble_phone.c: реклама перезапускается после connect, если
            // остался свободный слот из BLE_PHONE_MAX_CONNECTIONS). Поэтому RSSI
            // можно получать из пассивного скана — он живее, чем опрос раз в
            // RSSI_POLL_INTERVAL_MS. GATT-опрос (scheduleRssiPoll) остаётся как
            // резерв на случай, если все слоты заняты и advertising встал.
            startSignalMonitoring()
        } else if (signalMonitoring) {
            stopSignalMonitoring()
        }
    }

    private fun setAdminAvailability(available: Boolean) {
        handler.post { listener.onAdminAvailability(available) }
    }

    private fun fail(message: String) = update(State.ERROR, message)

    private fun closeGatt() {
        stopRssiPolling()
        adminProbePending = false
        pendingConfigVerification = null
        lockService = null
        runCatching { gatt?.disconnect() }
        gatt?.close()
        gatt = null
    }

    companion object {
        private const val TAG = "BleLock"
        private const val SCAN_TIMEOUT_MS = 15_000L
        private const val DEPARTURE_SILENCE_MS = 20_000L
        private const val PHONE_NAME_MAX_BYTES = 20
        private const val RSSI_POLL_INTERVAL_MS = 2_000L
        private const val WEAK_SIGNAL_LOG_INTERVAL_MS = 3_000L
        private const val WEAK_RSSI_DISCONNECT_STREAK = 3
        /* Совпадает с CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=256 в прошивке ESP32. */
        private const val REQUESTED_MTU = 256
    }
}
