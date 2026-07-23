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
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.SystemClock

class BleLockService : Service(), BleRepository.Listener {
    private val handler = Handler(Looper.getMainLooper())
    private lateinit var repository: BleRepository
    private lateinit var settings: LockSettings
    private var openSentForPresence = false
    private var stopping = false
    private var repositoryActive = false
    private var bluetoothAvailable = false
    private val bluetoothStateReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action != BluetoothAdapter.ACTION_STATE_CHANGED) return
            when (intent.getIntExtra(BluetoothAdapter.EXTRA_STATE, BluetoothAdapter.ERROR)) {
                BluetoothAdapter.STATE_OFF -> handleBluetoothOff()
                BluetoothAdapter.STATE_ON -> handleBluetoothOn()
            }
        }
    }
    private val departureRecovery = Runnable {
        if (!stopping && settings.autoOpenEnabled && openSentForPresence) {
            openSentForPresence = false
            repository.findAndConnect()
        }
    }

    override fun onCreate() {
        super.onCreate()
        settings = LockSettings(this)
        createNotificationChannel()
        startAsForeground(getString(R.string.service_starting))
        repository = BleRepository(this, this, controlOnly = true)
        repositoryActive = true
        bluetoothAvailable = getSystemService(BluetoothManager::class.java)
            .adapter?.isEnabled == true
        val stateFilter = IntentFilter(BluetoothAdapter.ACTION_STATE_CHANGED)
        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(bluetoothStateReceiver, stateFilter, RECEIVER_NOT_EXPORTED)
        } else {
            registerReceiver(bluetoothStateReceiver, stateFilter)
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
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
        scheduleSearch(0)
        return START_STICKY
    }

    override fun onDestroy() {
        stopping = true
        handler.removeCallbacksAndMessages(null)
        if (repositoryActive) repository.close()
        runCatching { unregisterReceiver(bluetoothStateReceiver) }
        super.onDestroy()
    }

    override fun onTaskRemoved(rootIntent: Intent?) {
        if (settings.autoOpenEnabled && !stopping) scheduleServiceRecovery()
        super.onTaskRemoved(rootIntent)
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onBleState(state: BleRepository.State, detail: String) {
        updateNotification(detail)
        if (!bluetoothAvailable) return
        when (state) {
            BleRepository.State.READY -> {
                if (!openSentForPresence) repository.openNow()
            }
            BleRepository.State.OPEN_SENT -> openSentForPresence = true
            BleRepository.State.IDLE, BleRepository.State.ERROR -> {
                if (stopping) return
                if (openSentForPresence) {
                    handler.postDelayed({
                        repository.waitForDeparture()
                        // If ESP32 was rebooted while the phone stayed nearby,
                        // the old GATT session may not report a clean disconnect.
                        // Do not wait forever for a departure: re-discover the
                        // advertising lock and establish a fresh secured link.
                        handler.removeCallbacks(departureRecovery)
                        handler.postDelayed(departureRecovery, DEPARTURE_RECOVERY_MS)
                    }, RECONNECT_DELAY_MS)
                } else {
                    scheduleSearch(RECONNECT_DELAY_MS)
                }
            }
            BleRepository.State.DEPARTED -> {
                handler.removeCallbacks(departureRecovery)
                openSentForPresence = false
                scheduleSearch(RECONNECT_DELAY_MS)
            }
            else -> Unit
        }
    }

    override fun onLockData(info: String?, status: String?) {
        if (info != null && status != null) {
            updateNotification("$info · $status")
        }
    }

    override fun onSignalRssi(rssi: Int?) {
        sendBroadcast(
            Intent(ACTION_SIGNAL_RSSI)
                .setPackage(packageName)
                .putExtra(EXTRA_SIGNAL_VALID, rssi != null)
                .putExtra(EXTRA_SIGNAL_RSSI, rssi ?: 0)
        )
    }

    private fun scheduleSearch(delayMs: Long) {
        handler.removeCallbacks(searchRunnable)
        handler.postDelayed(searchRunnable, delayMs)
    }

    private val searchRunnable = Runnable {
        if (!stopping && settings.autoOpenEnabled && bluetoothAvailable && repositoryActive) {
            repository.findAndConnect()
        }
    }

    private fun handleBluetoothOff() {
        bluetoothAvailable = false
        handler.removeCallbacks(searchRunnable)
        handler.removeCallbacks(departureRecovery)
        if (repositoryActive) {
            repository.close()
            repositoryActive = false
        }
        updateNotification(getString(R.string.bluetooth_disabled))
    }

    private fun handleBluetoothOn() {
        if (stopping || !settings.autoOpenEnabled) return
        bluetoothAvailable = true
        if (repositoryActive) repository.close()
        repository = BleRepository(this, this, controlOnly = true)
        repositoryActive = true
        openSentForPresence = false
        updateNotification(getString(R.string.service_bluetooth_restored))
        scheduleSearch(BLUETOOTH_RESTART_DELAY_MS)
    }

    private fun stopSelfSafely() {
        stopping = true
        handler.removeCallbacksAndMessages(null)
        if (::repository.isInitialized && repositoryActive) repository.close()
        repositoryActive = false
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
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
            PackageManager.PERMISSION_GRANTED
    }

    companion object {
        const val ACTION_START = "com.blelock.app.action.START"
        const val ACTION_STOP = "com.blelock.app.action.STOP"
        const val ACTION_SIGNAL_RSSI = "com.blelock.app.action.SIGNAL_RSSI"
        const val EXTRA_SIGNAL_VALID = "signal_valid"
        const val EXTRA_SIGNAL_RSSI = "signal_rssi"
        private const val CHANNEL_ID = "ble_lock_service"
        private const val NOTIFICATION_ID = 1001
        private const val RECONNECT_DELAY_MS = 3_000L
        private const val BLUETOOTH_RESTART_DELAY_MS = 500L
        private const val DEPARTURE_RECOVERY_MS = 30_000L
        private const val SERVICE_RECOVERY_DELAY_MS = 5_000L
        private const val RESTART_REQUEST_CODE = 1002
    }
}
