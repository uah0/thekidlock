package com.blelock.app

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanSettings
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.util.Log
import java.util.concurrent.atomic.AtomicBoolean

/** Background discovery for AUTO. GATT and authentication remain in BleLockService. */
internal object PassiveBleScan {
    private const val TAG = "BlePassiveScan"
    private const val REQUEST_CODE = 3107

    fun register(context: Context): Boolean {
        val appSettings = LockSettings(context)
        if (appSettings.simulatorModeEnabled || !appSettings.autoOpenEnabled) {
            stop(context)
            return false
        }
        if (!hasScanPermission(context)) {
            Log.w(TAG, "BLE_SCAN_ERROR: BLUETOOTH_SCAN/location permission missing")
            return false
        }
        val bluetoothOn = runCatching {
            context.getSystemService(BluetoothManager::class.java)?.adapter?.isEnabled == true
        }.getOrDefault(false)
        if (!BleScanPolicy.shouldRegister(true, true, bluetoothOn)) {
            Log.w(TAG, "BLE_SCAN_ERROR: Bluetooth is off")
            return false
        }
        val scanner = runCatching {
            context.getSystemService(BluetoothManager::class.java)
                ?.adapter?.takeIf { it.isEnabled }?.bluetoothLeScanner
        }.getOrElse {
            Log.e(TAG, "BLE_SCAN_ERROR: Bluetooth adapter unavailable", it)
            null
        }
        if (scanner == null) {
            Log.w(TAG, "BLE_SCAN_ERROR: Bluetooth is off or BLE scanner unavailable")
            return false
        }

        val filter = ScanFilter.Builder()
            .setServiceUuid(BleProtocol.SERVICE_PARCEL_UUID)
            .build()
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_POWER)
            .build()
        return runCatching {
            val result = scanner.startScan(listOf(filter), settings, pendingIntent(context))
            if (result == 0 || result == android.bluetooth.le.ScanCallback.SCAN_FAILED_ALREADY_STARTED) {
                Log.i(TAG, "PASSIVE_SCAN_REGISTERED: service UUID filter, LOW_POWER, status=$result")
                true
            } else {
                Log.e(TAG, "BLE_SCAN_ERROR: PendingIntent startScan status=$result")
                false
            }
        }.getOrElse {
            Log.e(TAG, "BLE_SCAN_ERROR: unable to register PendingIntent scan", it)
            false
        }
    }

    fun stop(context: Context) {
        if (!hasScanPermission(context)) return
        runCatching {
            val scanner = context.getSystemService(BluetoothManager::class.java)
                ?.adapter?.bluetoothLeScanner
            if (scanner != null) scanner.stopScan(pendingIntent(context))
            Log.i(TAG, "PASSIVE_SCAN_STOPPED")
        }.onFailure { Log.w(TAG, "BLE_SCAN_ERROR: unable to stop PendingIntent scan", it) }
    }

    fun hasScanPermission(context: Context): Boolean {
        val permission = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            Manifest.permission.BLUETOOTH_SCAN
        } else {
            Manifest.permission.ACCESS_FINE_LOCATION
        }
        return context.checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED
    }

    private fun pendingIntent(context: Context): PendingIntent {
        val intent = Intent(context, BleScanReceiver::class.java)
            .setAction(BleScanReceiver.ACTION_SCAN_RESULT)
            .setPackage(context.packageName)
        var flags = PendingIntent.FLAG_UPDATE_CURRENT
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            // BluetoothLeScanner fills scan results into this PendingIntent.
            flags = flags or PendingIntent.FLAG_MUTABLE
        }
        return PendingIntent.getBroadcast(context, REQUEST_CODE, intent, flags)
    }
}

/** Small, synchronized guard shared by all receiver instances in this process. */
internal class BleDetectionGate {
    private val active = AtomicBoolean(false)

    fun tryAcquire(): Boolean = active.compareAndSet(false, true)

    fun release() {
        active.set(false)
    }
}

internal object BleScanPolicy {
    const val SERVICE_UUID = "6f4f0001-9a7b-4c21-8d32-5e6f708192a3"
    const val MAX_RESULT_AGE_NANOS = 15_000_000_000L

    fun shouldRegister(autoOpenEnabled: Boolean, hasPermission: Boolean, bluetoothOn: Boolean) =
        autoOpenEnabled && hasPermission && bluetoothOn

    fun hasLockServiceUuid(advertisedUuids: Iterable<String>?): Boolean =
        advertisedUuids?.any { it.equals(SERVICE_UUID, ignoreCase = true) } == true

    fun isFreshLockAdvertisement(ageNanos: Long, advertisedUuids: Iterable<String>?): Boolean =
        ageNanos in 0..MAX_RESULT_AGE_NANOS && hasLockServiceUuid(advertisedUuids)

    fun isFreshLockResult(
        ageNanos: Long,
        advertisedUuids: Iterable<String>?,
        rssi: Int,
        threshold: Int
    ): Boolean = isFreshLockAdvertisement(ageNanos, advertisedUuids) && rssi >= threshold
}

class BleScanReceiver : android.content.BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        when (intent.action) {
            BluetoothAdapter.ACTION_STATE_CHANGED -> {
                when (intent.getIntExtra(BluetoothAdapter.EXTRA_STATE, BluetoothAdapter.ERROR)) {
                    BluetoothAdapter.STATE_OFF -> {
                        PassiveBleScan.stop(context)
                        Log.i(TAG, "PASSIVE_SCAN_STOPPED: Bluetooth off")
                    }
                    BluetoothAdapter.STATE_ON -> {
                        if (LockSettings(context).autoOpenEnabled) {
                            Log.i(TAG, "Bluetooth on: ForegroundService owns AUTO callback scanning")
                        }
                    }
                }
            }
            ACTION_SCAN_RESULT -> receiveScanResult(context, intent)
        }
    }

    private fun receiveScanResult(context: Context, intent: Intent) {
        Log.i(TAG, "BLE_PENDING_INTENT_RECEIVED")
        if (LockSettings(context).simulatorModeEnabled) {
            PassiveBleScan.stop(context)
            gate.release()
            Log.i(TAG, "Ignoring scan event: lock simulator is active")
            return
        }
        val error = intent.getIntExtra(
            android.bluetooth.le.BluetoothLeScanner.EXTRA_ERROR_CODE,
            0
        )
        if (error != 0) {
            Log.e(TAG, "BLE_SCAN_ERROR: PendingIntent result error=$error")
            return
        }
        if (!LockSettings(context).autoOpenEnabled) {
            Log.i(TAG, "Ignoring scan event: automatic mode is off")
            PassiveBleScan.stop(context)
            gate.release()
            return
        }

        val results = if (Build.VERSION.SDK_INT >= 33) {
            intent.getParcelableArrayListExtra(
                android.bluetooth.le.BluetoothLeScanner.EXTRA_LIST_SCAN_RESULT,
                android.bluetooth.le.ScanResult::class.java
            )
        } else {
            @Suppress("DEPRECATION")
            intent.getParcelableArrayListExtra(
                android.bluetooth.le.BluetoothLeScanner.EXTRA_LIST_SCAN_RESULT
            )
        }.orEmpty()

        val threshold = LockSettings(context).rssiThreshold
        val nowNanos = android.os.SystemClock.elapsedRealtimeNanos()
        val freshLockResults = results.filter { scanResult ->
            val ageNanos = nowNanos - scanResult.timestampNanos
            BleScanPolicy.isFreshLockAdvertisement(
                ageNanos, scanResult.scanRecord?.serviceUuids?.map { it.uuid.toString() }
            )
        }
        val result = freshLockResults.firstOrNull { scanResult ->
            val ageNanos = nowNanos - scanResult.timestampNanos
            BleScanPolicy.isFreshLockResult(
                ageNanos,
                scanResult.scanRecord?.serviceUuids?.map { it.uuid.toString() },
                scanResult.rssi,
                threshold
            )
        }
        if (result == null) {
            if (freshLockResults.isNotEmpty()) {
                val strongestRssi = freshLockResults.maxOf { it.rssi }
                Log.i(TAG, "Ignoring result: rssi below threshold (rssi=$strongestRssi, threshold=$threshold)")
            } else {
                Log.i(TAG, "Ignoring empty, stale, or non-lock scan result")
            }
            return
        }
        if (!gate.tryAcquire()) {
            Log.i(TAG, "ACTIVE_CONNECT_SKIPPED_ALREADY_RUNNING")
            return
        }

        Log.i(TAG, "BLE_DEVICE_DETECTED: ${result.device.address}, rssi=${result.rssi}")
        PassiveBleScan.stop(context)
        val serviceIntent = Intent(context, BleLockService::class.java)
            .setAction(BleLockService.ACTION_DEVICE_DETECTED)
            .putExtra(BleLockService.EXTRA_DETECTED_DEVICE, result.device)
        runCatching {
            // AUTO keeps the connectedDevice service alive while armed. This
            // updates that service without starting a new FGS from a background
            // BLE broadcast on Android 12+.
            context.startService(serviceIntent)
        }.onFailure {
            Log.e(TAG, "ACTIVE_CONNECT_FAILED: service delivery failed", it)
            gate.release()
        }
    }

    companion object {
        const val ACTION_SCAN_RESULT = "com.blelock.app.action.PASSIVE_SCAN_RESULT"
        private const val TAG = "BleScanReceiver"
        internal val gate = BleDetectionGate()
    }
}
