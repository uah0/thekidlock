package com.bleforeva.diagnostic

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.bluetooth.le.AdvertiseCallback
import android.bluetooth.le.AdvertiseData
import android.bluetooth.le.AdvertiseSettings
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.os.Build
import android.os.IBinder
import java.time.Instant
import java.time.LocalDateTime
import java.time.ZoneId
import java.time.format.DateTimeFormatter

class BeaconForegroundService : Service() {
    companion object {
        const val START = "com.bleforeva.diagnostic.START"
        const val STOP = "com.bleforeva.diagnostic.STOP"
    }
    private var callback: AdvertiseCallback? = null
    private var session = 0
    private var started = ""
    private var activeMode = BeaconMode.LOW_POWER
    private val receiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                BluetoothAdapter.ACTION_STATE_CHANGED -> {
                    val on = intent.getIntExtra(BluetoothAdapter.EXTRA_STATE, -1) == BluetoothAdapter.STATE_ON
                    val off = intent.getIntExtra(BluetoothAdapter.EXTRA_STATE, -1) == BluetoothAdapter.STATE_OFF
                    if (off) { BeaconState.log(context, "BLUETOOTH_OFF"); stopAdvertising(); setStatus("ERROR", "Bluetooth OFF") }
                    if (on) { BeaconState.log(context, "BLUETOOTH_ON"); startAdvertising("BLUETOOTH_RESTART") }
                }
                Intent.ACTION_SCREEN_ON -> BeaconState.log(context, "SCREEN_ON")
                Intent.ACTION_SCREEN_OFF -> BeaconState.log(context, "SCREEN_OFF")
                Intent.ACTION_USER_PRESENT -> BeaconState.log(context, "USER_PRESENT")
            }
        }
    }

    override fun onCreate() {
        super.onCreate()
        BeaconState.log(this, "SERVICE_CREATE", BeaconState.deviceInfo())
        getSystemService(NotificationManager::class.java).createNotificationChannel(
            NotificationChannel(BeaconState.CHANNEL, "BLE Foreva beacon", NotificationManager.IMPORTANCE_LOW)
        )
        val filter = IntentFilter().apply {
            addAction(BluetoothAdapter.ACTION_STATE_CHANGED)
            addAction(Intent.ACTION_SCREEN_ON)
            addAction(Intent.ACTION_SCREEN_OFF)
            addAction(Intent.ACTION_USER_PRESENT)
        }
        if (Build.VERSION.SDK_INT >= 33) registerReceiver(receiver, filter, RECEIVER_NOT_EXPORTED)
        else @Suppress("DEPRECATION") registerReceiver(receiver, filter)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        BeaconState.log(this, "SERVICE_START_COMMAND", intent?.action ?: "null/redelivery")
        if (intent?.action == STOP) {
            BeaconState.prefs(this).edit().putBoolean("wanted", false).apply()
            stopAdvertising()
            setStatus("STOPPED", "")
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelf()
            return START_NOT_STICKY
        }
        if (intent == null && !BeaconState.prefs(this).getBoolean("wanted", false)) {
            stopSelf()
            return START_NOT_STICKY
        }
        // A fresh session exposes service restarts and Bluetooth cycling in scanner logs.
        stopAdvertising()
        session = BeaconState.newSession()
        started = Instant.now().toString()
        val p = BeaconState.prefs(this)
        activeMode = BeaconMode.from(p.getString(if (intent == null) "active_mode" else "selected_mode", null))
        BeaconState.prefs(this).edit().putBoolean("wanted", true)
            .putInt("session", session).putString("started", started)
            .putString("active_mode", activeMode.name).apply()
        startForeground(1, notification("Starting beacon"))
        if (intent == null) BeaconState.log(this, "SESSION_RESTORED_SERVICE", "new_session=${BeaconState.hex(session)}")
        startAdvertising(if (intent == null) "SERVICE_RESTART" else "MANUAL_START")
        return START_STICKY
    }

    private fun startAdvertising(reason: String) {
        stopAdvertising()
        if (reason == "BLUETOOTH_RESTART") {
            session = BeaconState.newSession()
            started = Instant.now().toString()
            BeaconState.prefs(this).edit().putInt("session", session).putString("started", started).apply()
        }
        try {
            val manager = getSystemService(BluetoothManager::class.java)
            val adapter = manager.adapter ?: error("Bluetooth adapter missing")
            if (!adapter.isEnabled) error("Bluetooth OFF")
            val advertiser = adapter.bluetoothLeAdvertiser ?: error("BLE advertiser unsupported")
            val settings = AdvertiseSettings.Builder()
                .setAdvertiseMode(activeMode.advertiseMode)
                .setTxPowerLevel(AdvertiseSettings.ADVERTISE_TX_POWER_ULTRA_LOW)
                .setConnectable(false).setTimeout(0).build()
            val data = AdvertiseData.Builder()
                .addManufacturerData(BeaconState.COMPANY_ID, BeaconState.payload(BeaconState.deviceId(this), session))
                .setIncludeDeviceName(false).setIncludeTxPowerLevel(false).build()
            val thisCallback = object : AdvertiseCallback() {
                override fun onStartSuccess(settingsInEffect: AdvertiseSettings) {
                    if (callback !== this) return
                    BeaconState.log(this@BeaconForegroundService, "ADVERTISE_START_SUCCESS", "session=${BeaconState.hex(session)} mode=${activeMode.name}")
                    setStatus("ACTIVE", "")
                    getSystemService(NotificationManager::class.java).notify(1, notification("Beacon active"))
                }
                override fun onStartFailure(errorCode: Int) {
                    if (callback !== this) return
                    BeaconState.log(this@BeaconForegroundService, "ADVERTISE_START_FAILURE", "code=$errorCode mode=${activeMode.name}")
                    setStatus("ERROR", "Advertiser error $errorCode")
                    getSystemService(NotificationManager::class.java).notify(1, notification("Beacon error $errorCode"))
                }
            }
            callback = thisCallback
            BeaconState.log(this, "START_ADVERTISING_REQUEST", "reason=$reason session=${BeaconState.hex(session)} mode=${activeMode.name}")
            advertiser.startAdvertising(settings, data, thisCallback)
        } catch (e: Exception) {
            BeaconState.log(this, "ADVERTISE_START_FAILURE", e.toString())
            setStatus("ERROR", e.toString())
        }
    }

    private fun stopAdvertising() {
        val old = callback ?: return
        callback = null
        try { getSystemService(BluetoothManager::class.java).adapter?.bluetoothLeAdvertiser?.stopAdvertising(old) }
        catch (e: SecurityException) { BeaconState.log(this, "STOP_ADVERTISING_PERMISSION_LOST", e.toString()) }
        catch (e: Exception) { BeaconState.log(this, "STOP_ADVERTISING_ERROR", e.toString()) }
    }
    private fun setStatus(status: String, error: String) {
        BeaconState.prefs(this).edit().putString("status", status).putString("error", error)
            .putString("callback", Instant.now().toString()).apply()
    }
    private fun notification(line: String): Notification {
        val stop = PendingIntent.getService(this, 2, Intent(this, javaClass).setAction(STOP),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
        val open = PendingIntent.getActivity(this, 3, Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
        val time = try { LocalDateTime.ofInstant(Instant.parse(started), ZoneId.systemDefault())
            .format(DateTimeFormatter.ofPattern("HH:mm:ss")) } catch (_: Exception) { "unknown" }
        return Notification.Builder(this, BeaconState.CHANNEL)
            .setSmallIcon(android.R.drawable.ic_dialog_info).setContentTitle("BLE Foreva")
            .setContentText("$line · ${activeMode.name} · Session: ${BeaconState.hex(session)}")
            .setStyle(Notification.BigTextStyle().bigText("$line\nMode: ${activeMode.name}\nSession: ${BeaconState.hex(session)}\nStarted: $time"))
            .setContentIntent(open).setOngoing(true)
            .addAction(Notification.Action.Builder(null, "STOP", stop).build()).build()
    }
    override fun onTaskRemoved(rootIntent: Intent?) { BeaconState.log(this, "TASK_REMOVED"); super.onTaskRemoved(rootIntent) }
    override fun onDestroy() {
        BeaconState.log(this, "SERVICE_DESTROY")
        stopAdvertising()
        unregisterReceiver(receiver)
        super.onDestroy()
    }
    override fun onBind(intent: Intent?): IBinder? = null
}
