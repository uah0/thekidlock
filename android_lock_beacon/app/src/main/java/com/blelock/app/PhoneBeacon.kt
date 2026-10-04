package com.blelock.app

import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattServer
import android.bluetooth.BluetoothGattServerCallback
import android.bluetooth.BluetoothGattService
import android.bluetooth.BluetoothManager
import android.bluetooth.le.AdvertiseCallback
import android.bluetooth.le.AdvertiseData
import android.bluetooth.le.BluetoothLeAdvertiser
import android.bluetooth.le.AdvertiseSettings
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import android.os.SystemClock
import android.util.Log
import java.security.SecureRandom
import java.util.UUID

/** Discovery data is public. Only a bonded, encrypted GATT read confirms presence. */
class PhoneBeacon(private val context: Context) {
    companion object {
        private const val TAG = "PhoneBeacon"
        const val MANUFACTURER_ID = 0xF0E1 // Experimental identifier, not a Bluetooth SIG allocation.
        val SERVICE_UUID: UUID = UUID.fromString("6f4f0010-9a7b-4c21-8d32-5e6f708192a3")
        val PROOF_UUID: UUID = UUID.fromString("6f4f0011-9a7b-4c21-8d32-5e6f708192a3")
        val OWNER_STATUS_UUID: UUID = UUID.fromString("6f4f0012-9a7b-4c21-8d32-5e6f708192a3")
        val WIFI_SETUP_UUID: UUID = UUID.fromString("6f4f0013-9a7b-4c21-8d32-5e6f708192a3")
        val WEB_UI_IP_UUID: UUID = UUID.fromString("6f4f001a-9a7b-4c21-8d32-5e6f708192a3")
        private const val WIFI_SETUP_CHUNK_SIZE = 20
        private val WIFI_SETUP_CHUNK_UUIDS = listOf(
            WIFI_SETUP_UUID,
            UUID.fromString("6f4f0016-9a7b-4c21-8d32-5e6f708192a3"),
            UUID.fromString("6f4f0017-9a7b-4c21-8d32-5e6f708192a3"),
            UUID.fromString("6f4f0018-9a7b-4c21-8d32-5e6f708192a3"),
            UUID.fromString("6f4f0019-9a7b-4c21-8d32-5e6f708192a3")
        )
        val HOTSPOT_IP_UUID: UUID = BleProtocol.PHONE_HOTSPOT_IP_UUID
        val NAME_UUID: UUID = BleProtocol.PHONE_BEACON_NAME_UUID
        val PROOF = "BLE_LOCK_V1".toByteArray(Charsets.US_ASCII)
        const val ACTION_OWNER_STATUS_CHANGED = "com.blelock.app.OWNER_STATUS_CHANGED"
        const val ACTION_WIFI_PROVISIONING_DISCONNECTED = "com.blelock.app.WIFI_PROVISIONING_DISCONNECTED"
        const val ACTION_WEB_UI_IP_RECEIVED = "com.blelock.app.WEB_UI_IP_RECEIVED"
        const val EXTRA_WEB_UI_IP = "web_ui_ip"
    }

    private val bluetoothManager = context.getSystemService(BluetoothManager::class.java)
    private var server: BluetoothGattServer? = null
    private var advertiserCallback: AdvertiseCallback? = null
    private var activeAdvertiser: BluetoothLeAdvertiser? = null
    private var serverGeneration = 0
    private var serviceReady = false
    private var advertisingStarting = false
    private var advertisingStopRequested = false
    private val connectedPeers = LinkedHashMap<String, BluetoothDevice>()
    private val connectedPeersLock = Any()
    private var bondReceiverRegistered = false
    private var wanted = false
    @Volatile private var autoOpenWanted = false
    private var wifiProvisioningRequested = false
    @Volatile private var webUiIpRequest = false
    private var provisioningCredentials: ByteArray? = null
    private var provisioningReadPeer: String? = null
    @Volatile private var hotspotIpBytes: ByteArray? = null
    private var sessionId = 0

    val isWifiProvisioningActive: Boolean get() = wifiProvisioningRequested
    val isWebUiIpRequestActive: Boolean get() = webUiIpRequest

    private fun serverCallback(generation: Int) = object : BluetoothGattServerCallback() {
        override fun onConnectionStateChange(device: BluetoothDevice, status: Int, newState: Int) {
            synchronized(this@PhoneBeacon) {
                if (generation != serverGeneration) return

                val bondState = try { device.bondState } catch (_: SecurityException) { -1 }
                val address = runCatching { device.address }.getOrNull()
                Log.i(TAG, "GATT link address=$address deviceType=${runCatching { device.type }.getOrDefault(-1)} " +
                    "status=$status newState=$newState bondState=$bondState")
                if (address != null) synchronized(connectedPeersLock) {
                    if (newState == BluetoothGatt.STATE_CONNECTED) connectedPeers[address] = device
                    else if (newState == BluetoothGatt.STATE_DISCONNECTED) connectedPeers.remove(address)
                }
                if (newState == BluetoothGatt.STATE_CONNECTED && !autoOpenWanted &&
                    !isWifiProvisioningActive && !webUiIpRequest) {
                    runCatching { server?.cancelConnection(device) }
                        .onFailure { Log.w(TAG, "Could not reject GATT connection while Auto Open is off", it) }
                }
                if (newState == BluetoothGatt.STATE_DISCONNECTED && (autoOpenWanted || webUiIpRequest) &&
                    !isWifiProvisioningActive && advertiserCallback == null && !hasConnectedPeers()) {
                    startAdvertising()
                }
                if (newState != BluetoothGatt.STATE_DISCONNECTED || provisioningCredentials == null) return
                val peer = runCatching { device.address }.getOrNull()
                if (provisioningReadPeer == null || peer != provisioningReadPeer) {
                    Log.i(TAG, "Ignoring disconnect before Wi-Fi credentials were read by this peer")
                    return
                }
                provisioningCredentials = null
                provisioningReadPeer = null
                wifiProvisioningRequested = false
                if (autoOpenWanted || webUiIpRequest) restartAdvertising() else stop()
                Log.i(TAG, "Wi-Fi provisioning read completed; returning to normal beacon mode")
                context.sendBroadcast(
                    Intent(ACTION_WIFI_PROVISIONING_DISCONNECTED).setPackage(context.packageName)
                )

            }
        }

        override fun onServiceAdded(status: Int, service: BluetoothGattService) {
            synchronized(this@PhoneBeacon) {
                if (generation != serverGeneration) return

                if (!wanted || service.uuid != SERVICE_UUID) return
                if (status == BluetoothGatt.GATT_SUCCESS) {
                    serviceReady = true
                    startAdvertising()
                }
                else Log.e(TAG, "GATT service registration failed: $status")

            }
        }

        override fun onCharacteristicReadRequest(
            device: BluetoothDevice, requestId: Int, offset: Int,
            characteristic: BluetoothGattCharacteristic
        ) {
            synchronized(this@PhoneBeacon) {
                if (generation != serverGeneration) return

                val requestStartedNanos = SystemClock.elapsedRealtimeNanos()
                val current = server ?: return
                val bonded = try { device.bondState == BluetoothDevice.BOND_BONDED }
                catch (_: SecurityException) { false }
                val savedAutoEnabled = LockSettings(context).autoOpenEnabled
                val effectiveAutoOpen = autoOpenWanted && savedAutoEnabled
                val proofRead = wanted && (effectiveAutoOpen || wifiProvisioningRequested || webUiIpRequest) && bonded &&
                    characteristic.uuid == PROOF_UUID && offset <= PROOF.size
                val wifiValue = provisioningCredentials
                val wifiChunkIndex = WIFI_SETUP_CHUNK_UUIDS.indexOf(characteristic.uuid)
                val wifiChunkStart = wifiChunkIndex * WIFI_SETUP_CHUNK_SIZE
                val wifiChunkEnd = minOf(wifiChunkStart + WIFI_SETUP_CHUNK_SIZE, wifiValue?.size ?: 0)
                val wifiRead = wanted && wifiProvisioningRequested && bonded && wifiValue != null &&
                    wifiChunkIndex >= 0 && wifiChunkStart < wifiValue.size && offset <= wifiChunkEnd - wifiChunkStart
                val hotspotIpValue = hotspotIpBytes ?: ByteArray(4)
                val hotspotIpRead = wanted && wifiProvisioningRequested && bonded && characteristic.uuid == HOTSPOT_IP_UUID &&
                    offset <= hotspotIpValue.size
                val nameValue = if (characteristic.uuid == NAME_UUID) currentNameBytes() else null
                val nameRead = wanted && autoOpenWanted && bonded && nameValue != null && offset <= nameValue.size
                val autoOpenDisabledProof = wanted && !effectiveAutoOpen && bonded && characteristic.uuid == PROOF_UUID
                val status = when {
                    proofRead || wifiRead || hotspotIpRead || nameRead -> BluetoothGatt.GATT_SUCCESS
                    autoOpenDisabledProof -> BluetoothGatt.GATT_READ_NOT_PERMITTED
                    else -> BluetoothGatt.GATT_FAILURE
                }
                val bytes = when {
                    proofRead -> PROOF
                    wifiRead -> {
                        provisioningReadPeer = try { device.address }
                        catch (_: SecurityException) { null }
                        wifiValue!!.copyOfRange(wifiChunkStart, wifiChunkEnd)
                    }
                    hotspotIpRead -> hotspotIpValue
                    nameRead -> nameValue
                    else -> null
                }
                Log.i(
                    TAG,
                    "GATT read request id=$requestId uuid=${characteristic.uuid} deviceType=${device.type} " +
                        "bondState=${device.bondState} bonded=$bonded wanted=$wanted " +
                        "autoOpenWanted=$autoOpenWanted savedAuto=$savedAutoEnabled offset=$offset"
                )
                val value = bytes?.copyOfRange(offset, bytes.size)
                try {
                    val accepted = current.sendResponse(device, requestId, status, offset, value)
                    val elapsedUs = (SystemClock.elapsedRealtimeNanos() - requestStartedNanos) / 1_000L
                    Log.i(TAG, "GATT read response id=$requestId uuid=${characteristic.uuid} " +
                        "status=$status bytes=${value?.size ?: 0} accepted=$accepted elapsedUs=$elapsedUs")
                } catch (e: SecurityException) {
                    Log.e(TAG, "GATT read response denied id=$requestId uuid=${characteristic.uuid}", e)
                }

            }
        }

        override fun onCharacteristicWriteRequest(
            device: BluetoothDevice, requestId: Int, characteristic: BluetoothGattCharacteristic,
            preparedWrite: Boolean, responseNeeded: Boolean, offset: Int, value: ByteArray
        ) {
            synchronized(this@PhoneBeacon) {
                if (generation != serverGeneration) return

                val current = server ?: return
                val bonded = try { device.bondState == BluetoothDevice.BOND_BONDED }
                catch (_: SecurityException) { false }
                val valid = wanted && autoOpenWanted && bonded && characteristic.uuid == OWNER_STATUS_UUID &&
                    !preparedWrite && offset == 0 && value.size == 1 && (value[0] == 0.toByte() || value[0] == 1.toByte())
                if (valid) {
                    LockSettings(context).ownerAccessConfirmed = value[0] == 1.toByte()
                    context.sendBroadcast(
                        Intent(ACTION_OWNER_STATUS_CHANGED).setPackage(context.packageName)
                    )
                }
                val webUiIp = if (wanted && bonded && characteristic.uuid == WEB_UI_IP_UUID &&
                    !preparedWrite && offset == 0) parseWebUiIp(value) else null
                if (webUiIp != null) {
                    LockSettings(context).lockWifiIp = webUiIp
                    Log.i(TAG, "ESP Web UI IPv4 received over encrypted BLE")
                    context.sendBroadcast(
                        Intent(ACTION_WEB_UI_IP_RECEIVED)
                            .setPackage(context.packageName)
                            .putExtra(EXTRA_WEB_UI_IP, webUiIp)
                    )
                }
                if (responseNeeded) {
                    val status = if (valid || webUiIp != null) BluetoothGatt.GATT_SUCCESS else BluetoothGatt.GATT_FAILURE
                    try { current.sendResponse(device, requestId, status, offset, null) }
                    catch (e: SecurityException) { Log.e(TAG, "GATT owner status response denied", e) }
                }

            }
        }
    }

    private fun parseWebUiIp(bytes: ByteArray): String? {
        if (bytes.isEmpty() || bytes.size > 15) return null
        val ip = bytes.toString(Charsets.US_ASCII)
        val octets = ip.split('.')
        if (octets.size != 4 || octets.any { part ->
                part.isEmpty() || part.length > 3 || part.any { !it.isDigit() } ||
                    part.toIntOrNull()?.let { it !in 0..255 } != false
            }) return null
        if (octets.all { it.toIntOrNull() == 0 } || octets[0].toIntOrNull() == 127) return null
        return ip
    }

    private val bondStateReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            if (intent?.action != BluetoothDevice.ACTION_BOND_STATE_CHANGED) return
            val device = if (Build.VERSION.SDK_INT >= 33) {
                intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE, BluetoothDevice::class.java)
            } else {
                @Suppress("DEPRECATION")
                intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
            } ?: return
            val state = intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, -1)
            val previous = intent.getIntExtra(BluetoothDevice.EXTRA_PREVIOUS_BOND_STATE, -1)
            Log.i(TAG, "System bond change address=${runCatching { device.address }.getOrNull()} " +
                "deviceType=${runCatching { device.type }.getOrDefault(-1)} previous=$previous state=$state")
        }
    }

    @Synchronized
    fun start() {
        autoOpenWanted = true
        if (wanted) {
            if (!isWifiProvisioningActive) {
                if (hasConnectedPeers()) Log.i(TAG, "Auto Open resumed; waiting for previous GATT peer to disconnect")
                else restartAdvertising()
            }
            return
        }
        startServer()
    }

    @Synchronized
    fun startWebUiIpRequest() {
        webUiIpRequest = true
        if (!wanted) startServer() else restartAdvertising()
    }

    @Synchronized
    fun finishWebUiIpRequest() {
        webUiIpRequest = false
        if (wifiProvisioningRequested) return
        if (autoOpenWanted) {
            if (!wanted) startServer() else restartAdvertising()
        } else {
            stop()
        }
    }

    @Synchronized
    fun startWifiProvisioning(ssid: String, password: String, hotspotIp: ByteArray?) {
        val ssidBytes = ssid.toByteArray(Charsets.UTF_8)
        val passwordBytes = password.toByteArray(Charsets.UTF_8)
        require(ssidBytes.isNotEmpty() && ssidBytes.size <= 32) { "Invalid Wi-Fi SSID length" }
        require(passwordBytes.size <= 64) { "Invalid Wi-Fi password length" }
        wifiProvisioningRequested = true
        provisioningCredentials = byteArrayOf(ssidBytes.size.toByte(), passwordBytes.size.toByte()) +
            ssidBytes + passwordBytes
        Log.i(TAG, "Wi-Fi credential buffer ssidBytes=${ssidBytes.size} passwordBytes=${passwordBytes.size} nulAt=${provisioningCredentials?.indexOf(0) ?: -1}")
        provisioningReadPeer = null
        setHotspotIp(hotspotIp)
        if (!wanted) startServer() else restartAdvertising()
    }

    /** Suppress ordinary AUTO advertising while Android creates/resolves its hotspot. */
    @Synchronized
    fun prepareWifiProvisioning(resumeAutoOpen: Boolean) {
        autoOpenWanted = resumeAutoOpen
        wifiProvisioningRequested = true
        provisioningCredentials = null
        provisioningReadPeer = null
        setHotspotIp(null)
        stopAdvertising()
        Log.i(TAG, "AUTO beacon paused while hotspot is being prepared")
    }

    @Synchronized
    fun cancelWifiProvisioning() {
        if (!wifiProvisioningRequested) return
        wifiProvisioningRequested = false
        provisioningCredentials = null
        provisioningReadPeer = null
        setHotspotIp(null)
        if (autoOpenWanted || webUiIpRequest) {
            if (!wanted) startServer() else restartAdvertising()
        } else stop()
        Log.i(TAG, "Wi-Fi provisioning cancelled; beacon restored")
    }

    @Synchronized
    fun setHotspotIp(address: ByteArray?) {
        hotspotIpBytes = address?.takeIf { it.size == 4 && it.any { octet -> octet != 0.toByte() } }?.copyOf()
    }

    @Synchronized
    fun stopAutoOpen() {
        autoOpenWanted = false
        if (isWifiProvisioningActive || webUiIpRequest) {
            if (provisioningCredentials != null || webUiIpRequest) restartAdvertising()
            else {
                stopAdvertising()
            }
        } else {
            val currentServer = server
            val peers = synchronized(connectedPeersLock) { connectedPeers.values.toList() }
            try {
                stopAdvertising()
                peers.forEach { peer ->
                    runCatching { currentServer?.cancelConnection(peer) }
                        .onFailure { Log.w(TAG, "Could not disconnect GATT peer while Auto Open is off", it) }
                }
                Log.i(TAG, "Auto Open paused; advertising stopped, GATT server retained, disconnect requested for ${peers.size} peer(s)")
            } catch (e: Exception) { Log.w(TAG, "Could not pause PhoneBeacon", e) }
        }
    }

    @Synchronized
    private fun startServer() {
        if (wanted) return
        val adapter = bluetoothManager.adapter ?: return
        if (!adapter.isEnabled || adapter.bluetoothLeAdvertiser == null) return
        wanted = true
        if (!bondReceiverRegistered) {
            val filter = IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED)
            if (Build.VERSION.SDK_INT >= 33) {
                context.registerReceiver(bondStateReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
            } else {
                @Suppress("DEPRECATION")
                context.registerReceiver(bondStateReceiver, filter)
            }
            bondReceiverRegistered = true
        }
        serverGeneration++
        serviceReady = false
        sessionId = SecureRandom().nextInt()
        try {
            val characteristic = BluetoothGattCharacteristic(
                PROOF_UUID,
                BluetoothGattCharacteristic.PROPERTY_READ,
                BluetoothGattCharacteristic.PERMISSION_READ_ENCRYPTED
            )
            val service = BluetoothGattService(SERVICE_UUID, BluetoothGattService.SERVICE_TYPE_PRIMARY)
            service.addCharacteristic(characteristic)
            service.addCharacteristic(
                BluetoothGattCharacteristic(
                    NAME_UUID,
                    BluetoothGattCharacteristic.PROPERTY_READ,
                    BluetoothGattCharacteristic.PERMISSION_READ_ENCRYPTED
                )
            )
            service.addCharacteristic(
                BluetoothGattCharacteristic(
                    OWNER_STATUS_UUID,
                    BluetoothGattCharacteristic.PROPERTY_WRITE,
                    BluetoothGattCharacteristic.PERMISSION_WRITE_ENCRYPTED
                )
            )
            service.addCharacteristic(
                BluetoothGattCharacteristic(
                    WEB_UI_IP_UUID,
                    BluetoothGattCharacteristic.PROPERTY_WRITE,
                    BluetoothGattCharacteristic.PERMISSION_WRITE_ENCRYPTED
                )
            )
            WIFI_SETUP_CHUNK_UUIDS.forEach { uuid ->
                service.addCharacteristic(
                    BluetoothGattCharacteristic(
                        uuid,
                        BluetoothGattCharacteristic.PROPERTY_READ,
                        BluetoothGattCharacteristic.PERMISSION_READ_ENCRYPTED
                    )
                )
            }
            service.addCharacteristic(
                BluetoothGattCharacteristic(
                    HOTSPOT_IP_UUID,
                    BluetoothGattCharacteristic.PROPERTY_READ,
                    BluetoothGattCharacteristic.PERMISSION_READ_ENCRYPTED
                )
            )
            server = bluetoothManager.openGattServer(context, serverCallback(serverGeneration))
            if (server?.addService(service) != true) {
                Log.e(TAG, "GATT service could not be added")
                stop()
            }
        } catch (e: Exception) {
            Log.e(TAG, "Phone beacon startup failed", e)
            stop()
        }
    }

    private fun currentNameBytes(): ByteArray {
        val bluetoothName = try { bluetoothManager.adapter?.name }
        catch (_: SecurityException) { null }
        val name = (bluetoothName?.takeIf { it.isNotBlank() } ?: Build.MODEL)
            .replace(Regex("[\\u0000-\\u001F,]"), " ").trim().ifEmpty { "Android" }
        val bytes = name.toByteArray(Charsets.UTF_8)
        if (bytes.size <= 48) return bytes
        var end = 48
        while (end > 0 && (bytes[end].toInt() and 0xC0) == 0x80) end--
        return bytes.copyOf(end)
    }

    @Synchronized
    private fun startAdvertising() {
        if (!wanted || !serviceReady || advertiserCallback != null) return
        if (!wifiProvisioningRequested && !webUiIpRequest &&
            (!autoOpenWanted || !LockSettings(context).autoOpenEnabled)) return
        val adapter = bluetoothManager.adapter ?: return
        val advertiser = adapter.bluetoothLeAdvertiser ?: return
        val txPowerLevel = when (LockSettings(context).phoneBeaconTxPowerLevel) {
            LockSettings.MIN_PHONE_BEACON_TX_POWER_LEVEL -> AdvertiseSettings.ADVERTISE_TX_POWER_ULTRA_LOW
            1 -> AdvertiseSettings.ADVERTISE_TX_POWER_LOW
            2 -> AdvertiseSettings.ADVERTISE_TX_POWER_MEDIUM
            else -> AdvertiseSettings.ADVERTISE_TX_POWER_HIGH
        }
        val id = context.getSharedPreferences("phone_beacon", Context.MODE_PRIVATE).let { prefs ->
            val old = prefs.getInt("device_id", 0)
            if (old != 0) old else SecureRandom().nextInt().let { fresh ->
                prefs.edit().putInt("device_id", fresh).apply()
                fresh
            }
        }
        val modeByte = when {
            provisioningCredentials != null -> 0x81.toByte()
            webUiIpRequest -> 0x82.toByte()
            else -> 1.toByte()
        }
        val payload = byteArrayOf('L'.code.toByte(), 'O'.code.toByte(), 'C'.code.toByte(), 'K'.code.toByte(), modeByte) +
            intBytes(id) + intBytes(sessionId)
        val settings = AdvertiseSettings.Builder()
            .setAdvertiseMode(AdvertiseSettings.ADVERTISE_MODE_LOW_LATENCY)
            .setTxPowerLevel(txPowerLevel)
            .setConnectable(true).setTimeout(0).build()
        val data = AdvertiseData.Builder().addManufacturerData(MANUFACTURER_ID, payload)
            .setIncludeDeviceName(false).setIncludeTxPowerLevel(false).build()
        val callback = object : AdvertiseCallback() {
            override fun onStartSuccess(settingsInEffect: AdvertiseSettings) {
                synchronized(this@PhoneBeacon) {
                    if (advertiserCallback !== this) return
                    advertisingStarting = false
                    if (advertisingStopRequested) {
                        if (stopAdvertising() && wanted && serviceReady &&
                            (autoOpenWanted || webUiIpRequest || provisioningCredentials != null) &&
                            (!wifiProvisioningRequested || provisioningCredentials != null)) {
                            startAdvertising()
                        }
                        return
                    }
                    Log.i(TAG, "Phone beacon active session=${sessionId.toUInt().toString(16)} " +
                        "mode=0x${modeByte.toUByte().toString(16)} auto=$autoOpenWanted " +
                        "webUiRequest=$webUiIpRequest provisioning=${provisioningCredentials != null}")
                }
            }
            override fun onStartFailure(errorCode: Int) {
                synchronized(this@PhoneBeacon) {
                    if (advertiserCallback !== this) return
                    val restart = advertisingStopRequested
                    advertiserCallback = null
                    activeAdvertiser = null
                    advertisingStarting = false
                    advertisingStopRequested = false
                    Log.e(TAG, "Phone beacon advertising failed: $errorCode")
                    if (restart && wanted && serviceReady &&
                        (!wifiProvisioningRequested || provisioningCredentials != null)) startAdvertising()
                }
            }
        }

        activeAdvertiser = advertiser
        advertiserCallback = callback
        advertisingStarting = true
        advertisingStopRequested = false
        try { advertiser.startAdvertising(settings, data, callback) }
        catch (e: Exception) {
            advertiserCallback = null
            activeAdvertiser = null
            advertisingStarting = false
            Log.e(TAG, "Phone beacon advertising request failed", e)
        }
    }

    /** Keep the callback until registration finishes; early cancellation can orphan an OEM advertising set. */
    @Synchronized
    private fun stopAdvertising(): Boolean {
        val callback = advertiserCallback ?: return true
        if (advertisingStarting) {
            advertisingStopRequested = true
            Log.i(TAG, "Beacon stop deferred until advertising registration completes")
            return false
        }
        try {
            activeAdvertiser?.stopAdvertising(callback)
        } catch (e: Exception) {
            Log.w(TAG, "Could not stop beacon advertising; retaining callback", e)
            return false
        }
        advertiserCallback = null
        activeAdvertiser = null
        advertisingStopRequested = false
        return true
    }

    @Synchronized
    private fun restartAdvertising() {
        if (stopAdvertising()) startAdvertising()
    }

    @Synchronized
    fun applyConfiguredTransmitPower() {
        if (advertiserCallback != null) restartAdvertising()
    }

    private fun hasConnectedPeers(): Boolean = synchronized(connectedPeersLock) {
        connectedPeers.isNotEmpty()
    }

    @Synchronized
    fun stop() {
        wanted = false
        serviceReady = false
        serverGeneration++
        autoOpenWanted = false
        wifiProvisioningRequested = false
        webUiIpRequest = false
        provisioningCredentials = null
        provisioningReadPeer = null
        hotspotIpBytes = null
        val currentServer = server
        val peers = synchronized(connectedPeersLock) {
            connectedPeers.values.toList().also { connectedPeers.clear() }
        }
        try {
            stopAdvertising()
            peers.forEach { peer ->
                runCatching { currentServer?.cancelConnection(peer) }
                    .onFailure { Log.w(TAG, "Could not cancel GATT connection during beacon stop", it) }
            }
            currentServer?.close()
            Log.i(TAG, "Phone beacon stopped; GATT disconnect requested for ${peers.size} peer(s)")
        } catch (e: Exception) { Log.w(TAG, "Phone beacon shutdown error", e) }
        server = null
        if (bondReceiverRegistered) {
            runCatching { context.unregisterReceiver(bondStateReceiver) }
            bondReceiverRegistered = false
        }
    }

    private fun intBytes(value: Int) = byteArrayOf(
        (value ushr 24).toByte(), (value ushr 16).toByte(),
        (value ushr 8).toByte(), value.toByte()
    )
}
