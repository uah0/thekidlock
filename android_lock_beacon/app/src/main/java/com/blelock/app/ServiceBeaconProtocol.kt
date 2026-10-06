package com.blelock.app

/** Application data over standard encrypted GATT. */
object ServiceBeaconProtocol {
    const val MODE = 0x83
    const val WIFI = 1
    const val WEB_IP = 2
    const val HOLD = 4
    const val HOME_WIFI = 8
    data class HoldRequest(val id: Int, val enabled: Boolean?) {
        init { require(id != 0) }
    }
    data class Result(val id: Int, val code: Int, val holdOpen: Boolean)

    fun request(wifi: Boolean, webIp: Boolean, hold: HoldRequest?, homeId: Int = 0): ByteArray {
        require(homeId == 0 || (!wifi && !webIp && hold == null))
        val flags = (if (wifi) WIFI else 0) or (if (webIp) WEB_IP else 0) or
            (if (hold != null) HOLD else 0) or (if (homeId != 0) HOME_WIFI else 0)
        val command = when (hold?.enabled) { true -> 1; false -> 2; null -> 0 }
        val id = hold?.id ?: homeId
        return byteArrayOf(1, flags.toByte(), command.toByte(),
            (id ushr 24).toByte(), (id ushr 16).toByte(), (id ushr 8).toByte(), id.toByte())
    }

    fun result(bytes: ByteArray, pending: HoldRequest?): Result? {
        if (pending == null || bytes.size != 7 || bytes[0] != 1.toByte() ||
            bytes[5].toInt() !in 0..3 || bytes[6].toInt() !in 0..1) return null
        val id = ((bytes[1].toInt() and 255) shl 24) or
            ((bytes[2].toInt() and 255) shl 16) or
            ((bytes[3].toInt() and 255) shl 8) or (bytes[4].toInt() and 255)
        if (id != pending.id) return null
        val response = Result(id, bytes[5].toInt(), bytes[6] == 1.toByte())
        if (response.code == 0 && pending.enabled != null && pending.enabled != response.holdOpen)
            return null
        return response
    }
}
