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
    private var departureMode = false
    private var adminProbePending = false
    private var signalMonitoring = false
    private var refreshBondsAfterControl = false
    private var pendingBondDevice: BluetoothDevice? = null

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            if (departureMode) {
                handler.removeCallbacks(departureConfirmed)
                handler.postDelayed(departureConfirmed, DEPARTURE_SILENCE_MS)
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
            if (gatt.device.bondState != BluetoothDevice.BOND_BONDED) {
                update(State.BONDING, "Запуск системного Pairing/Bonding")
                if (!gatt.device.createBond()) {
                    fail("Не удалось запустить системный Pairing/Bonding")
                }
            } else {
                readInfo()
            }
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
                    update(State.READY, "Настройки сохранены")
                } else fail("Настройки отклонены замком")
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
            handler.post {
                listener.onSignalRssi(if (statusCode == BluetoothGatt.GATT_SUCCESS) rssi else null)
            }
            scheduleRssiPoll()
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
        val savedDevice = savedAddress?.let { address ->
            runCatching { bluetoothManager?.adapter?.getRemoteDevice(address) }.getOrNull()
        }
        if (savedDevice != null) {
            update(State.CONNECTING, "Подключение к сохранённому замку")
            pairOrConnect(savedDevice)
            return
        }
        val scanner = bluetoothManager?.adapter?.bluetoothLeScanner
        if (scanner == null) {
            fail("Bluetooth выключен или BLE scanner недоступен")
            return
        }
        update(State.SCANNING, "Поиск BLE Lock…")
        val filter = ScanFilter.Builder().setServiceUuid(BleProtocol.SERVICE_PARCEL_UUID).build()
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
            .setScanMode(ScanSettings.SCAN_MODE_LOW_POWER)
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
        write(characteristic, value.toByteArray(Charsets.US_ASCII), currentGatt)
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
                update(State.READY, "Настройки замка прочитаны")
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

    private fun update(newState: State, detail: String) {
        Log.d(TAG, "state -> $newState ($detail)")
        state = newState
        handler.post { listener.onBleState(newState, detail) }
    }

    private fun setAdminAvailability(available: Boolean) {
        handler.post { listener.onAdminAvailability(available) }
    }

    private fun fail(message: String) = update(State.ERROR, message)

    private fun closeGatt() {
        stopRssiPolling()
        adminProbePending = false
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
    }
}
