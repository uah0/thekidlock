package com.bleforeva.diagnostic

import android.content.Context
import android.bluetooth.le.AdvertiseSettings
import android.os.Build
import java.io.File
import java.security.SecureRandom
import java.time.OffsetDateTime
import java.time.format.DateTimeFormatter

enum class BeaconMode(val label: String, val advertiseMode: Int) {
    LOW_POWER("LOW POWER — baseline", AdvertiseSettings.ADVERTISE_MODE_LOW_POWER),
    BALANCED("BALANCED", AdvertiseSettings.ADVERTISE_MODE_BALANCED),
    LOW_LATENCY("LOW LATENCY — higher battery use", AdvertiseSettings.ADVERTISE_MODE_LOW_LATENCY);

    companion object {
        fun from(value: String?): BeaconMode = entries.firstOrNull { it.name == value } ?: LOW_POWER
    }
}

object BeaconState {
    const val COMPANY_ID = 0xFFFF // Experimental ID only; not an assigned Bluetooth SIG company ID.
    const val CHANNEL = "ble_foreva"
    private const val PREFS = "beacon"
    private val random = SecureRandom()

    fun prefs(context: Context) = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
    fun hex(value: Int) = "%08X".format(value)
    fun deviceId(context: Context): Int {
        val p = prefs(context)
        if (p.contains("device_id")) return p.getInt("device_id", 0)
        val id = random.nextInt()
        p.edit().putInt("device_id", id).apply()
        return id
    }
    fun newSession(): Int = random.nextInt()
    fun payload(device: Int, session: Int): ByteArray = byteArrayOf(
        'F'.code.toByte(), 'O'.code.toByte(), 'R'.code.toByte(), 'E'.code.toByte(), 1,
        (device ushr 24).toByte(), (device ushr 16).toByte(), (device ushr 8).toByte(), device.toByte(),
        (session ushr 24).toByte(), (session ushr 16).toByte(), (session ushr 8).toByte(), session.toByte()
    )
    @Synchronized fun log(context: Context, event: String, detail: String = "") {
        val timestamp = OffsetDateTime.now().format(DateTimeFormatter.ISO_OFFSET_DATE_TIME)
        File(context.filesDir, "ble-foreva.log").appendText("$timestamp $event${if (detail.isEmpty()) "" else " $detail"}\n")
    }
    fun logFile(context: Context) = File(context.filesDir, "ble-foreva.log")
    fun deviceInfo() = "${Build.MANUFACTURER} ${Build.MODEL} Android ${Build.VERSION.RELEASE} SDK ${Build.VERSION.SDK_INT} app 1.0"
}
