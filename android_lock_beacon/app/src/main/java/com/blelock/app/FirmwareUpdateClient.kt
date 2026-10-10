package com.blelock.app

import android.content.Context
import android.util.AtomicFile
import java.io.File
import java.io.FileInputStream
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest

internal object FirmwareUpdateClient {
    const val MAX_OTA_IMAGE_BYTES = 0x1E0000L + 512
    const val DOWNLOAD_NAME = "ble_lock_stage1_s3.bin"
    private const val DOWNLOAD_TIMEOUT_MS = 25_000

    data class Metadata(val marker: String, val version: String? = null)

    internal fun releaseVersion(url: URL): String? {
        if (url.protocol != "https" || url.host != "github.com" || url.userInfo != null ||
            url.port != -1 || url.query != null || url.ref != null) return null
        return Regex("/uah0/thekidlock/releases/download/esp-v(10\\.0\\.[0-9]+)/ble_lock_stage1_s3\\.bin")
            .matchEntire(url.path)?.groupValues?.get(1)
    }

    private fun versionUrl(version: String): URL {
        if (!FirmwareTransferState.validVersion(version)) throw IOException("Некорректный номер прошивки")
        return URL("https://github.com/uah0/thekidlock/releases/download/esp-v$version/$DOWNLOAD_NAME")
    }

    private fun latestVersion(url: URL): String {
        val connection = (url.openConnection() as HttpURLConnection).apply {
            instanceFollowRedirects = false
            connectTimeout = DOWNLOAD_TIMEOUT_MS
            readTimeout = DOWNLOAD_TIMEOUT_MS
            requestMethod = "HEAD"
            setRequestProperty("User-Agent", "TheKidLock-Android")
        }
        try {
            if (connection.responseCode !in setOf(301, 302, 303, 307, 308)) throw IOException("Не удалось определить версию прошивки")
            val location = connection.getHeaderField("Location") ?: throw IOException("Нет адреса релиза")
            return releaseVersion(URL(url, location)) ?: throw IOException("Непредусмотренный адрес релиза")
        } finally { connection.disconnect() }
    }

    fun latestFirmwareUrl(): String = BuildConfig.OTA_FIRMWARE_URL

    internal fun isAllowedFirmwareSource(url: URL): Boolean {
        val host = url.host.lowercase()
        if (url.userInfo != null || url.ref != null) return false
        if (url.protocol == "http") return BuildConfig.DEBUG && host in setOf("127.0.0.1", "localhost")
        return url.protocol == "https" && host == "github.com" &&
            url.port == -1 && url.query == null &&
            url.path == "/uah0/thekidlock/releases/latest/download/ble_lock_stage1_s3.bin"
    }

    /** Validate every redirect before opening it, including HEAD requests. */
    internal fun openFirmwareConnection(requestedUrl: URL, method: String,
        connect: (URL) -> HttpURLConnection = { it.openConnection() as HttpURLConnection }
    ): HttpURLConnection {
        if (!isAllowedFirmwareSource(requestedUrl) && releaseVersion(requestedUrl) == null)
            throw IOException("Источник прошивки не входит в список разрешённых")
        val localTestHttp = requestedUrl.protocol == "http"
        var current = requestedUrl
        repeat(6) {
            val allowed = if (localTestHttp) current == requestedUrl else
                AppUpdatePolicy.allowedDownload(current) && current.ref == null &&
                    (current.host == "release-assets.githubusercontent.com" ||
                        isAllowedFirmwareSource(current) || releaseVersion(current) != null)
            if (!allowed) throw IOException("Источник перенаправил загрузку на непредусмотренный адрес")
            val connection = connect(current).apply {
                instanceFollowRedirects = false
                connectTimeout = DOWNLOAD_TIMEOUT_MS
                readTimeout = if (method == "HEAD") DOWNLOAD_TIMEOUT_MS else 60_000
                requestMethod = method
                setRequestProperty("User-Agent", "TheKidLock-Android")
                setRequestProperty("Accept", "application/octet-stream")
            }
            try {
                val status = connection.responseCode
                if (status == HttpURLConnection.HTTP_OK) return connection
                val location = connection.getHeaderField("Location")
                if (!localTestHttp && status in setOf(301, 302, 303, 307, 308) && location != null) {
                    current = URL(current, location)
                } else throw IOException("GitHub вернул код $status")
            } catch (error: Exception) {
                connection.disconnect()
                throw error
            }
            connection.disconnect()
        }
        throw IOException("Слишком много перенаправлений GitHub")
    }

    fun validateFirmwareLength(length: Long) {
        if (length <= 0 || length > MAX_OTA_IMAGE_BYTES) {
            throw IOException("Размер образа прошивки недопустим: $length байт")
        }
    }

    fun checkLatestFirmware(): Metadata {
        val requestedUrl = URL(latestFirmwareUrl())
        val localTestHttp = requestedUrl.protocol == "http" &&
            requestedUrl.host.lowercase() in setOf("127.0.0.1", "localhost")
        if (!isAllowedFirmwareSource(requestedUrl)) {
            throw IOException("Источник прошивки не входит в список разрешённых")
        }
        val version = if (localTestHttp) null else latestVersion(requestedUrl)
        val connection = openFirmwareConnection(if (version == null) requestedUrl else versionUrl(version), "HEAD")
        try {
            val code = connection.responseCode
            if (code != HttpURLConnection.HTTP_OK) {
                throw IOException("Проверка новой прошивки завершилась с кодом $code")
            }
            val finalUrl = connection.url
            val host = finalUrl.host.lowercase()
            val allowedFinalUrl = if (localTestHttp) {
                finalUrl.protocol == "http" && host in setOf("127.0.0.1", "localhost")
            } else {
                finalUrl.protocol == "https" &&
                    (host == "github.com" || host == "release-assets.githubusercontent.com")
            }
            if (!allowedFinalUrl) {
                throw IOException("Источник перенаправил проверку на непредусмотренный адрес")
            }
            val length = connection.contentLengthLong
            if (length > 0) validateFirmwareLength(length)
            val etag = connection.getHeaderField("ETag").orEmpty()
            val lastModified = connection.getHeaderField("Last-Modified").orEmpty()
            if (etag.isBlank() && lastModified.isBlank()) {
                throw IOException("Сервер не сообщил метку версии прошивки")
            }
            return Metadata(createVersionMarker(
                etag, lastModified, length, "${finalUrl.protocol}://${finalUrl.host}${finalUrl.path}"
            ), version)
        } finally {
            connection.disconnect()
        }
    }

    internal fun createVersionMarker(
        etag: String,
        lastModified: String,
        contentLength: Long,
        resourcePath: String
    ): String {
        val data = listOf(etag, lastModified, contentLength.toString(), resourcePath)
            .joinToString("\n")
        return MessageDigest.getInstance("SHA-256")
            .digest(data.toByteArray(Charsets.UTF_8))
            .joinToString("") { "%02x".format(it) }
    }

    internal fun shouldDownloadFirmware(
        previousMarker: String?, currentMarker: String, fileAvailable: Boolean
    ): Boolean = !fileAvailable || previousMarker != currentMarker

    private fun storedFirmware(context: Context) =
        AtomicFile(File(File(context.filesDir, "firmware"), DOWNLOAD_NAME))

    @Synchronized
    fun hasPublishedFirmware(context: Context): Boolean = try {
        storedFirmware(context).openRead().use { input ->
            validateEncryptedHeader(readHeader(input), input.channel.size())
        }
        true
    } catch (_: IOException) {
        false
    }

    @Synchronized
    fun openStoredFirmware(context: Context): FileInputStream {
        val input = storedFirmware(context).openRead()
        try {
            validateEncryptedHeader(readHeader(input), input.channel.size())
            input.channel.position(0)
            return input
        } catch (error: Exception) {
            input.close()
            throw error
        }
    }

    private fun readHeader(input: java.io.InputStream): ByteArray {
        val header = ByteArray(512)
        var count = 0
        while (count < header.size) {
            val read = input.read(header, count, header.size - count)
            if (read < 0) break
            count += read
        }
        return header.copyOf(count)
    }

    internal fun validateEncryptedHeader(header: ByteArray, length: Long) {
        validateFirmwareLength(length)
        val magic = byteArrayOf(0xcf.toByte(), 0xb6.toByte(), 0x88.toByte(), 0x07)
        if (header.size != 512 || !header.copyOfRange(0, 4).contentEquals(magic)) {
            throw IOException("Файл не является зашифрованной прошивкой замка")
        }
        var appLength = 0L
        for (i in 0..3) appLength = appLength or ((header[404 + i].toLong() and 255) shl (8 * i))
        if (appLength <= 0 || appLength != length - 512) {
            throw IOException("Загружен неполный образ прошивки")
        }
    }

    @Synchronized
    fun publishForWebUi(context: Context, source: File, version: String? = null) {
        source.inputStream().use { input ->
            validateEncryptedHeader(readHeader(input), source.length())
        }
        val destination = storedFirmware(context)
        if (!FirmwareTransferState.setDownloadedVersion(context, null)) throw IOException("Не удалось сохранить номер прошивки")
        val output = destination.startWrite()
        try {
            source.inputStream().use { it.copyTo(output) }
            destination.finishWrite(output)
        } catch (error: Exception) {
            destination.failWrite(output)
            throw error
        }
        if (!FirmwareTransferState.setDownloadedVersion(context, version)) throw IOException("Не удалось сохранить номер прошивки")
    }

    fun downloadLatest(target: File, version: String? = null): File {
        val requestedUrl = if (version == null) URL(latestFirmwareUrl()) else versionUrl(version)
        val localTestHttp = requestedUrl.protocol == "http" &&
            requestedUrl.host.lowercase() in setOf("127.0.0.1", "localhost")
        if (!isAllowedFirmwareSource(requestedUrl) && releaseVersion(requestedUrl) == null) {
            throw IOException("Источник прошивки не входит в список разрешённых")
        }
        val connection = openFirmwareConnection(requestedUrl, "GET")
        try {
            val code = connection.responseCode
            if (code != HttpURLConnection.HTTP_OK) {
                throw IOException("Загрузка образа прошивки завершилась с кодом $code")
            }
            val finalUrl = connection.url
            val host = finalUrl.host.lowercase()
            val allowedFinalUrl = if (localTestHttp) {
                finalUrl.protocol == "http" && host in setOf("127.0.0.1", "localhost")
            } else {
                finalUrl.protocol == "https" &&
                    (host == "github.com" || host == "release-assets.githubusercontent.com")
            }
            if (!allowedFinalUrl) {
                throw IOException("Источник перенаправил загрузку на непредусмотренный адрес")
            }

            val expectedLength = connection.contentLengthLong
            if (expectedLength > 0) validateFirmwareLength(expectedLength)
            var received = 0L
            connection.inputStream.use { input -> target.outputStream().use { output ->
                val buffer = ByteArray(16 * 1024)
                while (true) {
                    val count = input.read(buffer)
                    if (count < 0) break
                    received += count
                    if (received > MAX_OTA_IMAGE_BYTES) {
                        throw IOException("Образ больше размера OTA-раздела")
                    }
                    output.write(buffer, 0, count)
                }
            } }
            validateFirmwareLength(received)
            if (expectedLength > 0 && received != expectedLength) {
                throw IOException("Загружен неполный образ прошивки")
            }
            return target
        } finally {
            connection.disconnect()
        }
    }

}
