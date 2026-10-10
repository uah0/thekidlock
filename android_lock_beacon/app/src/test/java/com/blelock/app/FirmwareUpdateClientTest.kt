package com.blelock.app

import java.io.IOException
import java.net.URL
import java.net.HttpURLConnection
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
        assertEquals(BuildConfig.DEBUG, FirmwareUpdateClient.isAllowedFirmwareSource(
            URL("http://127.0.0.1:38483/firmware.bin")
        ))
        assertEquals(false, FirmwareUpdateClient.isAllowedFirmwareSource(
            URL("http://192.168.1.49:38483/firmware.bin")
        ))
        assertEquals(false, FirmwareUpdateClient.isAllowedFirmwareSource(
            URL("https://example.com/firmware.bin")
        ))
    }

    private val release = URL("https://github.com/uah0/thekidlock/releases/download/esp-v10.0.480/ble_lock_stage1_s3.bin")
    private class Response(url: URL, private val status: Int, private val location: String? = null,
                           private val fail: Boolean = false) : HttpURLConnection(url) {
        var disconnected = false
        override fun connect() {}
        override fun usingProxy() = false
        override fun disconnect() { disconnected = true }
        override fun getResponseCode(): Int {
            if (fail) throw IOException("Transport failed")
            return status
        }
        override fun getHeaderField(name: String?) = if (name == "Location") location else null
    }

    @Test fun forbiddenRedirectIsRejectedBeforeAnyRequestToItsHost() {
        for (method in listOf("GET", "HEAD")) {
            for (destination in listOf("http://github.com/a", "http://127.0.0.1/private",
                "https://github.com.evil/a", "https://example.com/a",
                "https://user@release-assets.githubusercontent.com/a", "https://github.com:444/a",
                "https://github.com/other/repo/releases/download/x/firmware.bin")) {
                val opened = mutableListOf<URL>()
                val redirect = Response(release, 302, destination)
                assertThrows(IOException::class.java) {
                    FirmwareUpdateClient.openFirmwareConnection(release, method) {
                        opened.add(it); redirect
                    }
                }
                assertEquals(listOf(release), opened)
                assertEquals(true, redirect.disconnected)
            }
        }
    }

    @Test fun allowedAssetRedirectPreservesMethodAndDisablesAutomaticRedirects() {
        for (method in listOf("GET", "HEAD")) {
            val asset = URL("https://release-assets.githubusercontent.com/asset.bin?signature=test")
            val redirect = Response(release, 302, asset.toString())
            val success = Response(asset, 200)
            val result = FirmwareUpdateClient.openFirmwareConnection(release, method) {
                if (it == release) redirect else success
            }
            assertEquals(success, result)
            assertEquals(true, redirect.disconnected)
            assertEquals(false, success.disconnected)
            assertEquals(false, success.instanceFollowRedirects)
            assertEquals(method, success.requestMethod)
            result.disconnect()
        }
    }

    @Test fun redirectLoopsAreBoundedAndEveryConnectionIsClosed() {
        val responses = mutableListOf<Response>()
        assertThrows(IOException::class.java) {
            FirmwareUpdateClient.openFirmwareConnection(release, "GET") {
                Response(it, 302, release.toString()).also(responses::add)
            }
        }
        assertEquals(6, responses.size)
        assertEquals(true, responses.all { it.disconnected })
    }

    @Test fun missingRedirectAndTransportErrorsCloseConnection() {
        for (response in listOf(Response(release, 302), Response(release, 404), Response(release, 200, fail = true))) {
            assertThrows(IOException::class.java) {
                FirmwareUpdateClient.openFirmwareConnection(release, "HEAD") { response }
            }
            assertEquals(true, response.disconnected)
        }
    }

    @Test fun credentialsPortsQueriesAndFragmentsCannotChangeThePinnedSource() {
        val base = FirmwareUpdateClient.latestFirmwareUrl()
        for (source in listOf(base.replace("https://", "https://user@"),
            base.replace("github.com", "github.com:443"), "$base?override=1", "$base#fragment")) {
            assertEquals(false, FirmwareUpdateClient.isAllowedFirmwareSource(URL(source)))
        }
        assertEquals(null, FirmwareUpdateClient.releaseVersion(URL("$release?override=1")))
        assertEquals(null, FirmwareUpdateClient.releaseVersion(URL("$release#fragment")))
    }

    @Test fun loopbackNeverEscapesAndIsUnavailableInRelease() {
        val local = URL("http://127.0.0.1:38483/firmware.bin")
        var opened = 0
        val redirect = Response(local, 302, release.toString())
        assertThrows(IOException::class.java) {
            FirmwareUpdateClient.openFirmwareConnection(local, "GET") { opened++; redirect }
        }
        assertEquals(if (BuildConfig.DEBUG) 1 else 0, opened)
        assertEquals(BuildConfig.DEBUG, redirect.disconnected)
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
