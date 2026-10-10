package com.blelock.app

import java.net.URL
import org.junit.Assert.*
import org.junit.Test

class FirmwareTransferStateTest {
    @Test fun comparesDownloadedVersionWithStartedTransferWithoutEspConfirmation() {
        assertTrue(FirmwareTransferState.versionsDiffer("10.0.440", "10.0.438"))
        assertTrue(FirmwareTransferState.versionsDiffer("10.0.440", null))
        assertFalse(FirmwareTransferState.versionsDiffer("10.0.440", "10.0.440"))
        assertFalse(FirmwareTransferState.versionsDiffer(null, "10.0.440"))
        assertFalse(FirmwareTransferState.versionsDiffer("bad", "10.0.440"))
    }

    @Test fun tracksLocksSeparatelyAndNormalizesAddressCase() {
        assertEquals(FirmwareTransferState.lockKey("aa:bb"), FirmwareTransferState.lockKey("AA:BB"))
        assertNotEquals(FirmwareTransferState.lockKey("aa:bb"), FirmwareTransferState.lockKey("aa:cc"))
    }

    @Test fun getsVersionOnlyFromPinnedGithubReleaseAsset() {
        assertEquals("10.0.440", FirmwareUpdateClient.releaseVersion(URL(
            "https://github.com/uah0/thekidlock/releases/download/esp-v10.0.440/ble_lock_stage1_s3.bin")))
        for (url in listOf(
            "https://example.com/uah0/thekidlock/releases/download/esp-v10.0.440/ble_lock_stage1_s3.bin",
            "http://github.com/uah0/thekidlock/releases/download/esp-v10.0.440/ble_lock_stage1_s3.bin",
            "https://github.com/other/thekidlock/releases/download/esp-v10.0.440/ble_lock_stage1_s3.bin",
            "https://github.com/uah0/thekidlock/releases/download/esp-v10.0.440/other.bin")) {
            assertNull(FirmwareUpdateClient.releaseVersion(URL(url)))
        }
    }
}
