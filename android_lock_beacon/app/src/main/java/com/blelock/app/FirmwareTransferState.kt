package com.blelock.app

import android.content.Context
import java.util.Locale

internal object FirmwareTransferState {
    private fun preferences(context: Context) =
        context.getSharedPreferences("firmware_transfer", Context.MODE_PRIVATE)

    fun downloadedVersion(context: Context): String? =
        preferences(context).getString("downloaded_version", null)

    fun currentLock(context: Context): String =
        InternetAccess(context).load()?.optString("ble_peer")?.takeIf { it.isNotBlank() }
            ?: LockSettings(context).lockAddress ?: "current_lock"

    fun setDownloadedVersion(context: Context, version: String?): Boolean =
        preferences(context).edit().putString("downloaded_version", version).commit()

    fun markStarted(context: Context, lock: String?, version: String): Boolean {
        if (lock.isNullOrBlank() || !validVersion(version)) return false
        return preferences(context).edit().putString(lockKey(lock), version).commit()
    }

    fun updateAvailable(context: Context, lock: String?): Boolean {
        if (lock.isNullOrBlank()) return false
        return versionsDiffer(downloadedVersion(context), preferences(context).getString(lockKey(lock), null))
    }

    internal fun lockKey(lock: String) = "sent_" + lock.uppercase(Locale.ROOT)
    internal fun validVersion(version: String?) = version != null && Regex("10\\.0\\.[0-9]+").matches(version)
    internal fun versionsDiffer(downloaded: String?, sent: String?) = validVersion(downloaded) && downloaded != sent
}
