package com.blelock.app

import android.os.ParcelUuid
import java.util.UUID

object BleProtocol {
    const val DEVICE_NAME = "BLE Lock"
    val SERVICE_UUID: UUID = UUID.fromString("6f4f0001-9a7b-4c21-8d32-5e6f708192a3")
    /** PhoneBeacon hotspot IPv4: four network-order bytes; 00 00 00 00 means unavailable. */
    val PHONE_HOTSPOT_IP_UUID: UUID = UUID.fromString("6f4f0014-9a7b-4c21-8d32-5e6f708192a3")
    /** UTF-8 Bluetooth name of a bonded phone; read over the encrypted PhoneBeacon service. */
    val PHONE_BEACON_NAME_UUID: UUID = UUID.fromString("6f4f0015-9a7b-4c21-8d32-5e6f708192a3")
    val INFO_UUID: UUID = UUID.fromString("6f4f0002-9a7b-4c21-8d32-5e6f708192a3")
    val COMMAND_UUID: UUID = UUID.fromString("6f4f0003-9a7b-4c21-8d32-5e6f708192a3")
    val STATUS_UUID: UUID = UUID.fromString("6f4f0004-9a7b-4c21-8d32-5e6f708192a3")
    val ADMIN_CONFIG_UUID: UUID = UUID.fromString("6f4f0006-9a7b-4c21-8d32-5e6f708192a3")
    val ADMIN_STATE_UUID: UUID = UUID.fromString("6f4f0007-9a7b-4c21-8d32-5e6f708192a3")
    val ADMIN_BONDS_UUID: UUID = UUID.fromString("6f4f0008-9a7b-4c21-8d32-5e6f708192a3")
    val ADMIN_CONTROL_UUID: UUID = UUID.fromString("6f4f0009-9a7b-4c21-8d32-5e6f708192a3")
    val ADMIN_EVENTS_UUID: UUID = UUID.fromString("6f4f000a-9a7b-4c21-8d32-5e6f708192a3")
    val ADMIN_SCHEMA_UUID: UUID = UUID.fromString("6f4f000e-9a7b-4c21-8d32-5e6f708192a3")
    val PHONE_NAME_UUID: UUID = UUID.fromString("6f4f000b-9a7b-4c21-8d32-5e6f708192a3")
    val OTA_CONFIG_UUID: UUID = UUID.fromString("6f4f000c-9a7b-4c21-8d32-5e6f708192a3")
    val OTA_STATUS_UUID: UUID = UUID.fromString("6f4f000d-9a7b-4c21-8d32-5e6f708192a3")
    const val OTA_PERSIST_PREFIX = "PERSIST\n"
    val SERVICE_PARCEL_UUID = ParcelUuid(SERVICE_UUID)
    val OPEN_COMMAND: ByteArray = "OPEN".toByteArray(Charsets.US_ASCII)
    val FORCE_OPEN_COMMAND: ByteArray = "FORCE_OPEN".toByteArray(Charsets.US_ASCII)
    val KEEP_OPEN_COMMAND: ByteArray = "KEEP_OPEN".toByteArray(Charsets.US_ASCII)
    val MODE_AUTO_COMMAND: ByteArray = "MODE_AUTO".toByteArray(Charsets.US_ASCII)
    val MODE_ALWAYS_COMMAND: ByteArray = "MODE_ALWAYS".toByteArray(Charsets.US_ASCII)
    val AUTO_OFF_CLOSE_COMMAND: ByteArray = "AUTO_OFF_CLOSE".toByteArray(Charsets.US_ASCII)
}
