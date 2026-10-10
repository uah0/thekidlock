package com.blelock.app

import android.content.Context
import android.content.pm.ApplicationInfo
import android.content.pm.PackageInfo
import android.content.pm.PackageManager
import android.os.Build
import android.util.AtomicFile
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest

internal object AppUpdateClient {
    private const val API = "https://api.github.com/repos/uah0/thekidlock/releases"
    private const val MANIFEST = "thekidlock-update.json"
    private val checkLock = Any()
    fun preferences(context: Context) = context.getSharedPreferences("app_updates", Context.MODE_PRIVATE)
    private fun directory(context: Context) = File(context.filesDir, "app-updates").apply { mkdirs() }
    fun apkFile(context: Context, metadata: AppUpdateMetadata) = File(directory(context), "release-${metadata.versionCode}.apk")
    private fun storedManifest(context: Context) = AtomicFile(File(directory(context), MANIFEST))

    @Suppress("DEPRECATION")
    private fun signingFlags() = if (Build.VERSION.SDK_INT >= 28) PackageManager.GET_SIGNING_CERTIFICATES
        else PackageManager.GET_SIGNATURES
    @Suppress("DEPRECATION")
    private fun code(info: PackageInfo): Long = if (Build.VERSION.SDK_INT >= 28) info.longVersionCode
        else info.versionCode.toLong()
    fun installedVersion(context: Context) = code(context.packageManager.getPackageInfo(context.packageName, 0))
    @Suppress("DEPRECATION")
    private fun certificates(info: PackageInfo): Set<String> {
        val signatures = if (Build.VERSION.SDK_INT >= 28) info.signingInfo?.apkContentsSigners else info.signatures
        return signatures.orEmpty().map {
            AppUpdatePolicy.hex(MessageDigest.getInstance("SHA-256").digest(it.toByteArray()))
        }.toSet()
    }

    private fun parse(text: String): AppUpdateMetadata {
        val json = JSONObject(text)
        if (json.getInt("schemaVersion") != 1 || json.getString("packageName") != "com.blelock.app") {
            throw IOException("Описание обновления не предназначено для TheKidLock")
        }
        val rawCode = json.getLong("versionCode")
        if (rawCode !in 1..2_100_000_000L) throw IOException("Некорректный versionCode")
        val metadata = AppUpdateMetadata(rawCode.toInt(), json.getString("versionName"),
            json.getLong("bytes"), json.getString("sha256"), json.getString("certificateSha256"),
            json.getString("notes")).validate()
        if (json.getString("apkUrl") != metadata.apkUrl) throw IOException("Непредусмотренный источник APK")
        return metadata
    }

    fun pending(context: Context): AppUpdateMetadata? = try {
        val metadata = parse(storedManifest(context).openRead().use { it.reader().readText() })
        metadata.takeIf { it.versionCode > installedVersion(context) && apkFile(context, it).length() == it.bytes }
    } catch (_: Exception) { null }

    fun cleanupInstalled(context: Context) {
        val installed = installedVersion(context)
        directory(context).listFiles()?.forEach { file ->
            val version = Regex("release-([0-9]+)\\.apk").matchEntire(file.name)?.groupValues?.get(1)?.toLongOrNull()
            if (version != null && version <= installed) file.delete()
        }
        val cached = try { parse(storedManifest(context).openRead().use { it.reader().readText() }) }
            catch (_: Exception) { null }
        if (cached != null && cached.versionCode <= installed) {
            storedManifest(context).delete()
            context.getSystemService(android.app.NotificationManager::class.java).cancel(AppUpdateWorker.NOTIFICATION_ID)
        }
    }

    // No authenticated URL or arbitrary host is accepted by the phone.
    private fun open(url: URL, api: Boolean = false): HttpURLConnection {
        var current = url
        repeat(6) {
            val allowed = if (api) current.protocol == "https" && current.host == "api.github.com" &&
                current.userInfo == null && current.port == -1 && current.path == "/repos/uah0/thekidlock/releases"
            else AppUpdatePolicy.allowedDownload(current)
            if (!allowed) throw IOException("Источник обновления не разрешён")
            val connection = (current.openConnection() as HttpURLConnection).apply {
                instanceFollowRedirects = false
                connectTimeout = 25_000
                readTimeout = 60_000
                useCaches = false
                setRequestProperty("User-Agent", "TheKidLock-Android")
                setRequestProperty("Cache-Control", "no-cache")
            }
            val status = try { connection.responseCode }
                catch (error: Exception) { connection.disconnect(); throw error }
            if (status == 200) return connection
            val location = connection.getHeaderField("Location")
            connection.disconnect()
            if (!api && status in setOf(301, 302, 303, 307, 308) && location != null) {
                current = URL(current, location)
            } else throw IOException("GitHub вернул код $status")
        }
        throw IOException("Слишком много перенаправлений GitHub")
    }

    private fun text(url: URL, api: Boolean = false): String {
        val connection = open(url, api)
        try {
            val bytes = connection.inputStream.use { it.readBytesLimited(256 * 1024) }
            return bytes.toString(Charsets.UTF_8)
        } finally { connection.disconnect() }
    }
    private fun java.io.InputStream.readBytesLimited(max: Int): ByteArray {
        val output = java.io.ByteArrayOutputStream()
        val buffer = ByteArray(8192)
        while (true) {
            val count = read(buffer)
            if (count < 0) return output.toByteArray()
            if (output.size() + count > max) throw IOException("Описание обновления слишком большое")
            output.write(buffer, 0, count)
        }
    }

    private fun latestCode(): Int? {
        var latest: Int? = null
        for (page in 1..5) {
            val releases = JSONArray(text(URL("$API?per_page=100&page=$page"), api = true))
            for (i in 0 until releases.length()) {
                val release = releases.getJSONObject(i)
                if (release.optBoolean("draft") || release.optBoolean("prerelease")) continue
                val version = AppUpdatePolicy.releaseCode(release.optString("tag_name")) ?: continue
                if (latest == null || version > latest) latest = version
            }
            if (releases.length() < 100) return latest
        }
        throw IOException("Слишком много релизов для проверки обновления")
    }

    fun validateApk(context: Context, file: File, metadata: AppUpdateMetadata) {
        file.inputStream().use { AppUpdatePolicy.copyVerified(it, null, metadata) }
        val archive = context.packageManager.getPackageArchiveInfo(file.absolutePath, signingFlags())
            ?: throw IOException("Загруженный файл не является APK")
        val current = context.packageManager.getPackageInfo(context.packageName, signingFlags())
        AppUpdatePolicy.validatePackage(metadata, archive.packageName, code(archive), archive.versionName,
            certificates(archive), certificates(current),
            (archive.applicationInfo?.flags ?: 0) and ApplicationInfo.FLAG_DEBUGGABLE != 0)
    }

    fun checkAndDownload(context: Context, force: Boolean, cancelled: () -> Boolean): AppUpdateMetadata? =
        synchronized(checkLock) {
            cleanupInstalled(context)
            val preferences = preferences(context)
            if (!force && System.currentTimeMillis() - preferences.getLong("last_checked", 0) < 15 * 60_000) {
                return@synchronized pending(context)
            }
            val version = latestCode()
            if (version == null || version <= installedVersion(context)) {
                preferences.edit().putLong("last_checked", System.currentTimeMillis()).remove("last_error").apply()
                return@synchronized pending(context)
            }
            val url = URL("https://github.com/uah0/thekidlock/releases/download/app-v1.0.$version/$MANIFEST")
            val manifest = text(url)
            val metadata = parse(manifest)
            if (metadata.versionCode != version) throw IOException("Версия описания не соответствует релизу")
            val target = apkFile(context, metadata)
            val already = pending(context)
            if (already == metadata) {
                validateApk(context, target, metadata)
            } else {
                val temporary = File(directory(context), "download.part")
                val connection = open(URL(metadata.apkUrl))
                try {
                    if (connection.contentLengthLong > 0 && connection.contentLengthLong != metadata.bytes) {
                        throw IOException("Размер ответа GitHub не соответствует APK")
                    }
                    connection.inputStream.use { input -> temporary.outputStream().use { output ->
                        AppUpdatePolicy.copyVerified(input, output, metadata, cancelled)
                    } }
                    validateApk(context, temporary, metadata)
                    if (cancelled()) throw IOException("Загрузка отменена")
                    if (target.exists() && !target.delete()) throw IOException("Не удалось заменить APK")
                    if (!temporary.renameTo(target)) throw IOException("Не удалось сохранить APK")
                    val store = storedManifest(context)
                    val output = store.startWrite()
                    try {
                        output.write(manifest.toByteArray(Charsets.UTF_8))
                        store.finishWrite(output)
                    } catch (error: Exception) { store.failWrite(output); throw error }
                } finally { connection.disconnect(); temporary.delete() }
            }
            // Keep the latest verified APK even if the user skips several releases.
            directory(context).listFiles()?.filter {
                it.name.matches(Regex("release-[0-9]+\\.apk")) && it != target
            }?.forEach { it.delete() }
            preferences.edit().putLong("last_checked", System.currentTimeMillis()).remove("last_error").apply()
            metadata
        }
}
