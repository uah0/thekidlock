package com.blelock.app

import java.io.IOException
import java.net.URL
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class FirmwareUpdateClientTest {
    @Test
    fun rejectsPlaintextAndTruncatedContainersBeforeStorage() {
        val header = ByteArray(512)
        byteArrayOf(0xcf.toByte(), 0xb6.toByte(), 0x88.toByte(), 0x07).copyInto(header)
        header[405] = 0x10 // 4096-byte signed application, little endian.
        FirmwareUpdateClient.validateEncryptedHeader(header, 4608)
        assertThrows(IOException::class.java) {
            FirmwareUpdateClient.validateEncryptedHeader(header, 4607)
        }
        assertThrows(IOException::class.java) {
            FirmwareUpdateClient.validateEncryptedHeader(header.copyOf(256), 4608)
        }
        header[0] = 0xe9.toByte()
        assertThrows(IOException::class.java) {
            FirmwareUpdateClient.validateEncryptedHeader(header, 4608)
        }
    }

    @Test
    fun releaseSourceIsFixedToTheProjectFirmwareAsset() {
        assertEquals(
            "https://github.com/uah0/thekidlock/releases/latest/download/ble_lock_stage1_s3.bin",
            FirmwareUpdateClient.latestFirmwareUrl()
        )
    }

    @Test
    fun onlyPinnedReleaseOrLoopbackTestSourceIsAllowed() {
        assertEquals(true, FirmwareUpdateClient.isAllowedFirmwareSource(
            URL("https://github.com/uah0/thekidlock/releases/latest/download/ble_lock_stage1_s3.bin")
        ))
        assertEquals(true, FirmwareUpdateClient.isAllowedFirmwareSource(
            URL("http://127.0.0.1:38483/firmware.bin")
        ))
        assertEquals(false, FirmwareUpdateClient.isAllowedFirmwareSource(
            URL("http://192.168.1.49:38483/firmware.bin")
        ))
        assertEquals(false, FirmwareUpdateClient.isAllowedFirmwareSource(
            URL("https://example.com/firmware.bin")
        ))
    }

    @Test
    fun otaImageMustFitTheExistingAppPartition() {
        FirmwareUpdateClient.validateFirmwareLength(FirmwareUpdateClient.MAX_OTA_IMAGE_BYTES)
        assertThrows(IOException::class.java) { FirmwareUpdateClient.validateFirmwareLength(0) }
        assertThrows(IOException::class.java) {
            FirmwareUpdateClient.validateFirmwareLength(FirmwareUpdateClient.MAX_OTA_IMAGE_BYTES + 1)
        }
    }

    @Test
    fun serverMetadataIdentifiesAReleaseAndTriggersPrefetchOnlyWhenChanged() {
        val marker = FirmwareUpdateClient.createVersionMarker(
            "\"etag-1\"", "Wed, 08 Oct 2026 21:34:02 GMT", 1_445_888,
            "https://release-assets.githubusercontent.com/asset.bin"
        )
        val sameMarker = FirmwareUpdateClient.createVersionMarker(
            "\"etag-1\"", "Wed, 08 Oct 2026 21:34:02 GMT", 1_445_888,
            "https://release-assets.githubusercontent.com/asset.bin"
        )
        val newMarker = FirmwareUpdateClient.createVersionMarker(
            "\"etag-2\"", "Wed, 08 Oct 2026 22:34:02 GMT", 1_445_888,
            "https://release-assets.githubusercontent.com/asset.bin"
        )

        assertEquals(marker, sameMarker)
        assertEquals(false, FirmwareUpdateClient.shouldDownloadFirmware(marker, sameMarker, true))
        assertEquals(true, FirmwareUpdateClient.shouldDownloadFirmware(marker, newMarker, true))
        assertEquals(true, FirmwareUpdateClient.shouldDownloadFirmware(newMarker, newMarker, false))
    }
}
