package com.blelock.app

import android.content.Context
import android.os.Build
import android.os.UserManager

class LockSettings(context: Context) {
    private val storageContext = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N &&
        !context.isDeviceProtectedStorage) {
        context.createDeviceProtectedStorageContext()
    } else context
    private val preferences = storageContext.getSharedPreferences("ble_lock", Context.MODE_PRIVATE)

    init {
        val userUnlocked = Build.VERSION.SDK_INT < Build.VERSION_CODES.N ||
            context.getSystemService(UserManager::class.java)?.isUserUnlocked == true
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N && userUnlocked &&
            !preferences.contains(KEY_AUTO_OPEN) && !context.isDeviceProtectedStorage) {
            val oldPreferences = context.getSharedPreferences("ble_lock", Context.MODE_PRIVATE)
            if (oldPreferences.contains(KEY_AUTO_OPEN)) {
                preferences.edit()
                    .putBoolean(KEY_AUTO_OPEN, oldPreferences.getBoolean(KEY_AUTO_OPEN, false))
                    .commit()
            }
        }
    }

    var autoOpenEnabled: Boolean
        get() = preferences.getBoolean(KEY_AUTO_OPEN, false)
        set(value) {
            preferences.edit().putBoolean(KEY_AUTO_OPEN, value).commit()
        }

    var batteryOptimizationNoticeShown: Boolean
        get() = preferences.getBoolean(KEY_BATTERY_NOTICE_SHOWN, false)
        set(value) {
            preferences.edit().putBoolean(KEY_BATTERY_NOTICE_SHOWN, value).commit()
        }

    var autoStartNoticeShown: Boolean
        get() = preferences.getBoolean(KEY_AUTOSTART_NOTICE_SHOWN, false)
        set(value) {
            preferences.edit().putBoolean(KEY_AUTOSTART_NOTICE_SHOWN, value).commit()
        }

    var lockAddress: String?
        get() = preferences.getString(KEY_LOCK_ADDRESS, null)
        set(value) {
            preferences.edit().putString(KEY_LOCK_ADDRESS, value).commit()
        }

    var webUiCountdownSeconds: Int
        get() = preferences.getInt(KEY_WEB_UI_COUNTDOWN_SECONDS, 10).coerceIn(1, 60)
        set(value) {
            preferences.edit().putInt(KEY_WEB_UI_COUNTDOWN_SECONDS, value.coerceIn(1, 60)).commit()
        }

    var rssiThreshold: Int
        get() = preferences.getInt(KEY_RSSI_THRESHOLD, DEFAULT_RSSI_THRESHOLD)
            .coerceIn(RSSI_THRESHOLD_MIN, RSSI_THRESHOLD_MAX)
        set(value) {
            preferences.edit().putInt(
                KEY_RSSI_THRESHOLD,
                value.coerceIn(RSSI_THRESHOLD_MIN, RSSI_THRESHOLD_MAX)
            ).commit()
        }

    var securitySettleDelayMs: Int
        get() = preferences.getInt(KEY_SECURITY_SETTLE_DELAY_MS, DEFAULT_SECURITY_SETTLE_DELAY_MS)
            .coerceIn(SECURITY_SETTLE_DELAY_MIN_MS, SECURITY_SETTLE_DELAY_MAX_MS)
        set(value) {
            preferences.edit().putInt(
                KEY_SECURITY_SETTLE_DELAY_MS,
                value.coerceIn(SECURITY_SETTLE_DELAY_MIN_MS, SECURITY_SETTLE_DELAY_MAX_MS)
            ).commit()
        }

    var autoScanWindowMs: Int
        get() = preferences.getInt(KEY_AUTO_SCAN_WINDOW_MS, DEFAULT_AUTO_SCAN_WINDOW_MS)
            .coerceIn(AUTO_SCAN_WINDOW_MIN_MS, AUTO_SCAN_WINDOW_MAX_MS)
        set(value) {
            preferences.edit().putInt(
                KEY_AUTO_SCAN_WINDOW_MS,
                value.coerceIn(AUTO_SCAN_WINDOW_MIN_MS, AUTO_SCAN_WINDOW_MAX_MS)
            ).commit()
        }

    var autoScanPauseMs: Int
        get() = preferences.getInt(KEY_AUTO_SCAN_PAUSE_MS, DEFAULT_AUTO_SCAN_PAUSE_MS)
            .coerceIn(AUTO_SCAN_PAUSE_MIN_MS, AUTO_SCAN_PAUSE_MAX_MS)
        set(value) {
            preferences.edit().putInt(
                KEY_AUTO_SCAN_PAUSE_MS,
                value.coerceIn(AUTO_SCAN_PAUSE_MIN_MS, AUTO_SCAN_PAUSE_MAX_MS)
            ).commit()
        }

    companion object {
        const val RSSI_THRESHOLD_MIN = -100
        const val RSSI_THRESHOLD_MAX = -30
        const val DEFAULT_RSSI_THRESHOLD = -75
        const val SECURITY_SETTLE_DELAY_MIN_MS = 0
        const val SECURITY_SETTLE_DELAY_MAX_MS = 2000
        const val DEFAULT_SECURITY_SETTLE_DELAY_MS = 700
        const val AUTO_SCAN_WINDOW_MIN_MS = 100
        const val AUTO_SCAN_WINDOW_MAX_MS = 15_000
        const val DEFAULT_AUTO_SCAN_WINDOW_MS = 15_000
        const val AUTO_SCAN_PAUSE_MIN_MS = 1_000
        const val AUTO_SCAN_PAUSE_MAX_MS = 30_000
        const val DEFAULT_AUTO_SCAN_PAUSE_MS = 3_000
        private const val KEY_AUTO_OPEN = "auto_open_enabled"
        private const val KEY_BATTERY_NOTICE_SHOWN = "battery_optimization_notice_shown"
        private const val KEY_AUTOSTART_NOTICE_SHOWN = "autostart_notice_shown"
        private const val KEY_LOCK_ADDRESS = "lock_address"
        private const val KEY_WEB_UI_COUNTDOWN_SECONDS = "web_ui_countdown_seconds"
        private const val KEY_RSSI_THRESHOLD = "rssi_threshold"
        private const val KEY_SECURITY_SETTLE_DELAY_MS = "security_settle_delay_ms"
        private const val KEY_AUTO_SCAN_WINDOW_MS = "auto_scan_window_ms"
        private const val KEY_AUTO_SCAN_PAUSE_MS = "auto_scan_pause_ms"
    }
}
