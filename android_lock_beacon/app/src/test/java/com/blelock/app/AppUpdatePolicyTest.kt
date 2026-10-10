package com.blelock.app

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.net.URL
import java.security.MessageDigest
import org.junit.Assert.*
import org.junit.Test

class AppUpdatePolicyTest {
    private val payload = "a verified APK fixture".toByteArray()
    private val certificate = "a".repeat(64)
    private fun metadata() = AppUpdateMetadata(288, "1.0.288", payload.size.toLong(),
        AppUpdatePolicy.hex(MessageDigest.getInstance("SHA-256").digest(payload)), certificate, "Изменения")

    @Test fun preservesVerifiedPayload() {
        val output = ByteArrayOutputStream()
        AppUpdatePolicy.copyVerified(ByteArrayInputStream(payload), output, metadata())
        assertArrayEquals(payload, output.toByteArray())
    }

    @Test fun rejectsTamperingTruncationAndExtraBytes() {
        for (bytes in listOf(payload.copyOf(payload.size - 1), payload + byteArrayOf(0),
            payload.copyOf().apply { this[0] = 0 })) {
            assertThrows(IOException::class.java) {
                AppUpdatePolicy.copyVerified(ByteArrayInputStream(bytes), null, metadata())
            }
        }
    }

    @Test fun cancelledDownloadIsNeverAccepted() {
        assertThrows(IOException::class.java) {
            AppUpdatePolicy.copyVerified(ByteArrayInputStream(payload), null, metadata()) { true }
        }
    }

    @Test fun rejectsInvalidManifestBoundsAndHashes() {
        for (invalid in listOf(metadata().copy(versionCode = 0), metadata().copy(versionName = "1.0.289"),
            metadata().copy(bytes = 0), metadata().copy(bytes = AppUpdatePolicy.MAX_APK_BYTES + 1),
            metadata().copy(sha256 = "bad"), metadata().copy(certificateSha256 = "bad"),
            metadata().copy(notes = "a".repeat(8001)))) {
            assertThrows(IOException::class.java) { invalid.validate() }
        }
    }

    @Test fun onlyProjectAppTagsAreVersions() {
        assertEquals(288, AppUpdatePolicy.releaseCode("app-v1.0.288"))
        for (tag in listOf("esp-v10.0.453", "app-v1.0.0", "app-v1.0.0288", "app-v1.0.2100000001", "app-v1.0.288/x"))
            assertNull(AppUpdatePolicy.releaseCode(tag))
    }

    @Test fun redirectsCannotEscapeHttpsGithubAssets() {
        assertTrue(AppUpdatePolicy.allowedDownload(URL(metadata().apkUrl)))
        assertTrue(AppUpdatePolicy.allowedDownload(URL("https://release-assets.githubusercontent.com/a.apk")))
        for (url in listOf("http://github.com/a", "https://github.com.evil/a", "https://example.com/a",
            "https://user@github.com/a", "https://github.com:444/a"))
            assertFalse(AppUpdatePolicy.allowedDownload(URL(url)))
    }

    @Test fun acceptsOnlyMatchingReleasePackageAndSignature() {
        AppUpdatePolicy.validatePackage(metadata(), "com.blelock.app", 288, "1.0.288",
            setOf(certificate), setOf(certificate), false)
        for (other in listOf(emptySet(), setOf("b".repeat(64)), setOf(certificate, "b".repeat(64)))) {
            assertThrows(IOException::class.java) {
                AppUpdatePolicy.validatePackage(metadata(), "com.blelock.app", 288, "1.0.288", other, setOf(certificate), false)
            }
        }
        assertThrows(IOException::class.java) {
            AppUpdatePolicy.validatePackage(metadata(), "other.app", 288, "1.0.288", setOf(certificate), setOf(certificate), false)
        }
        assertThrows(IOException::class.java) {
            AppUpdatePolicy.validatePackage(metadata(), "com.blelock.app", 287, "1.0.288", setOf(certificate), setOf(certificate), false)
        }
        assertThrows(IOException::class.java) {
            AppUpdatePolicy.validatePackage(metadata(), "com.blelock.app", 288, "1.0.288", setOf(certificate), setOf(certificate), true)
        }
    }
}
