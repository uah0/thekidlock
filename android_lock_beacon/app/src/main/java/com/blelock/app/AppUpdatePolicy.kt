package com.blelock.app

import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import java.net.URL
import java.security.MessageDigest

internal data class AppUpdateMetadata(
    val versionCode: Int,
    val versionName: String,
    val bytes: Long,
    val sha256: String,
    val certificateSha256: String,
    val notes: String
) {
    val tag get() = "app-v1.0.$versionCode"
    val apkUrl get() = "https://github.com/uah0/thekidlock/releases/download/$tag/thekidlock.apk"
    fun validate(): AppUpdateMetadata {
        if (versionCode !in 1..2_100_000_000 || versionName != "1.0.$versionCode" ||
            bytes !in 1..AppUpdatePolicy.MAX_APK_BYTES ||
            !Regex("[0-9a-f]{64}").matches(sha256) ||
            !Regex("[0-9a-f]{64}").matches(certificateSha256) || notes.length > 8_000) {
            throw IOException("Некорректное описание обновления приложения")
        }
        return this
    }
}

internal object AppUpdatePolicy {
    const val MAX_APK_BYTES = 64L * 1024 * 1024
    fun releaseCode(tag: String): Int? = Regex("app-v1\\.0\\.([1-9][0-9]{0,9})")
        .matchEntire(tag)?.groupValues?.get(1)?.toIntOrNull()?.takeIf { it <= 2_100_000_000 }

    fun allowedDownload(url: URL): Boolean = url.protocol == "https" &&
        url.userInfo == null && url.port in listOf(-1, 443) &&
        url.host in setOf("github.com", "release-assets.githubusercontent.com")

    fun copyVerified(input: InputStream, output: OutputStream?, metadata: AppUpdateMetadata,
                     cancelled: () -> Boolean = { false }) {
        metadata.validate()
        val digest = MessageDigest.getInstance("SHA-256")
        val buffer = ByteArray(16 * 1024)
        var count = 0L
        while (true) {
            if (cancelled()) throw IOException("Загрузка отменена")
            val read = input.read(buffer)
            if (read < 0) break
            count += read
            if (count > metadata.bytes) throw IOException("Размер APK превышает объявленный")
            digest.update(buffer, 0, read)
            output?.write(buffer, 0, read)
        }
        if (count != metadata.bytes || hex(digest.digest()) != metadata.sha256) {
            throw IOException("APK загружен не полностью или не прошёл проверку SHA-256")
        }
    }

    fun validatePackage(metadata: AppUpdateMetadata, packageName: String, versionCode: Long,
                        versionName: String?, certificates: Set<String>, installed: Set<String>,
                        debuggable: Boolean) {
        if (packageName != "com.blelock.app" || versionCode != metadata.versionCode.toLong() ||
            versionName != metadata.versionName || debuggable || certificates.isEmpty() ||
            certificates != installed || certificates != setOf(metadata.certificateSha256)) {
            throw IOException("Пакет, версия или подпись APK не соответствует приложению")
        }
    }

    fun hex(bytes: ByteArray) = bytes.joinToString("") { "%02x".format(it) }
}
