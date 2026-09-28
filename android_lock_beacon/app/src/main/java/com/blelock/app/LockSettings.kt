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
            .coerceIn(MIN_RSSI_THRESHOLD, MAX_RSSI_THRESHOLD)
        set(value) {
            preferences.edit()
                .putInt(KEY_RSSI_THRESHOLD, value.coerceIn(MIN_RSSI_THRESHOLD, MAX_RSSI_THRESHOLD))
                .commit()
        }

    companion object {
        private const val KEY_AUTO_OPEN = "auto_open_enabled"
        private const val KEY_BATTERY_NOTICE_SHOWN = "battery_optimization_notice_shown"
        private const val KEY_AUTOSTART_NOTICE_SHOWN = "autostart_notice_shown"
        private const val KEY_LOCK_ADDRESS = "lock_address"
        private const val KEY_WEB_UI_COUNTDOWN_SECONDS = "web_ui_countdown_seconds"
        private const val KEY_RSSI_THRESHOLD = "rssi_threshold"
        private const val DEFAULT_RSSI_THRESHOLD = -75
        private const val MIN_RSSI_THRESHOLD = -100
        private const val MAX_RSSI_THRESHOLD = -30
    }
}
