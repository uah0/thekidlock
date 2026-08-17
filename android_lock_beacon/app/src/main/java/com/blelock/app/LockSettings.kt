package com.blelock.app

import android.content.Context
import android.os.Build

class LockSettings(context: Context) {
    private val storageContext = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N &&
        !context.isDeviceProtectedStorage) {
        context.createDeviceProtectedStorageContext()
    } else context
    private val preferences = storageContext.getSharedPreferences("ble_lock", Context.MODE_PRIVATE)

    init {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N &&
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

    /** Порог RSSI (дБм) сигнала ESP32: подключение инициируется, только если
     * сигнал замка не слабее этого порога (например, -80 означает "не дальше -80 дБм"). */
    var connectRssiThreshold: Int
        get() = preferences.getInt(KEY_CONNECT_RSSI_THRESHOLD, DEFAULT_CONNECT_RSSI_THRESHOLD)
        set(value) {
            preferences.edit().putInt(KEY_CONNECT_RSSI_THRESHOLD, value).commit()
        }

    /** Порог RSSI (дБм) для отключения от уже подключённого замка. Должен быть
     * ниже (слабее) connectRssiThreshold, чтобы получить гистерезис и не дёргать
     * соединение туда-обратно на границе порога подключения. */
    var disconnectRssiThreshold: Int
        get() = preferences.getInt(KEY_DISCONNECT_RSSI_THRESHOLD, DEFAULT_DISCONNECT_RSSI_THRESHOLD)
        set(value) {
            preferences.edit().putInt(KEY_DISCONNECT_RSSI_THRESHOLD, value).commit()
        }

    companion object {
        private const val KEY_AUTO_OPEN = "auto_open_enabled"
        private const val KEY_BATTERY_NOTICE_SHOWN = "battery_optimization_notice_shown"
        private const val KEY_AUTOSTART_NOTICE_SHOWN = "autostart_notice_shown"
        private const val KEY_LOCK_ADDRESS = "lock_address"
        private const val KEY_CONNECT_RSSI_THRESHOLD = "connect_rssi_threshold"
        const val DEFAULT_CONNECT_RSSI_THRESHOLD = -85
        private const val KEY_DISCONNECT_RSSI_THRESHOLD = "disconnect_rssi_threshold"
        const val DEFAULT_DISCONNECT_RSSI_THRESHOLD = -95
    }
}
