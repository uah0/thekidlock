package com.blelock.app

import android.content.Context
import android.content.SharedPreferences
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import org.json.JSONObject
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/** LTK is never stored in plaintext, device-protected storage, logs, or backups. */
class InternetAccess(context: Context) {
    private val prefs = context.applicationContext.getSharedPreferences("internet_access", Context.MODE_PRIVATE)
    private val brokerHostKey = "broker_host"
    private fun wrappingKey(): SecretKey {
        val store = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        (store.getKey("blelock-internet-wrap", null) as? SecretKey)?.let { return it }
        return KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore").run {
            init(KeyGenParameterSpec.Builder("blelock-internet-wrap", KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM).setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE).build())
            generateKey()
        }
    }
    @Synchronized fun save(bytes: ByteArray, blePeer: String) {
        require(bytes.size in 1..512)
        val json = JSONObject(bytes.toString(Charsets.UTF_8))
        val target = json.getString("target")
        val phone = json.getString("phone")
        require(target.matches(Regex("[0-9a-f]{2}(:[0-9a-f]{2}){5}")))
        require(phone.matches(Regex("[01][0-9a-f]{12}")))
        require(json.getString("ltk").matches(Regex("[0-9a-f]{32}")))
        val incomingHost = json.getString("host")
        require(incomingHost.matches(Regex("[a-zA-Z0-9.-]{0,192}")))
        val existing = load()
        if (existing != null && existing.optString("target") == target && existing.optString("phone") == phone) {
            json.put("host", existing.optString("host", incomingHost))
        } else {
            prefs.getString(brokerHostKey, null)?.let { json.put("host", it) }
        }
        json.remove("port")
        json.getBoolean("enabled")
        json.put("ble_peer", blePeer)
        val plain = json.toString().toByteArray(Charsets.UTF_8)
        val sealed = try {
            val cipher = Cipher.getInstance("AES/GCM/NoPadding").apply { init(Cipher.ENCRYPT_MODE, wrappingKey()) }
            cipher.iv + cipher.doFinal(plain)
        } finally { plain.fill(0) }
        val floor = json.optString("last_id", "0").toLongOrNull() ?: 0
        check(prefs.edit().putString("sealed", Base64.encodeToString(sealed, Base64.NO_WRAP))
            .putLong("last_id", maxOf(floor, prefs.getLong("last_id", 0))).commit())
    }
    @Synchronized fun saveBrokerHost(host: String): Boolean {
        if (!host.matches(Regex("[a-zA-Z0-9.-]{1,192}"))) return false
        return prefs.edit().putString(brokerHostKey, host).commit()
    }
    fun load(): JSONObject? = runCatching {
        val sealed = Base64.decode(prefs.getString("sealed", null) ?: return null, Base64.NO_WRAP)
        require(sealed.size > 28)
        val cipher = Cipher.getInstance("AES/GCM/NoPadding").apply {
            init(Cipher.DECRYPT_MODE, wrappingKey(), GCMParameterSpec(128, sealed.copyOfRange(0, 12)))
        }
        val plain = cipher.doFinal(sealed.copyOfRange(12, sealed.size))
        try {
            JSONObject(plain.toString(Charsets.UTF_8)).apply {
                prefs.getString(brokerHostKey, null)?.let { put("host", it) }
            }
        } finally { plain.fill(0) }
    }.getOrNull()
    fun registerListener(listener: SharedPreferences.OnSharedPreferenceChangeListener) {
        prefs.registerOnSharedPreferenceChangeListener(listener)
    }
    fun unregisterListener(listener: SharedPreferences.OnSharedPreferenceChangeListener) {
        prefs.unregisterOnSharedPreferenceChangeListener(listener)
    }
    fun revoke(blePeer: String) {
        if (load()?.optString("ble_peer") == blePeer) prefs.edit().remove("sealed").commit()
    }
    @Synchronized fun nextId(): Long {
        val next = maxOf(System.currentTimeMillis(), prefs.getLong("last_id", 0) + 1)
        require(next in 1..9007199254740991L)
        check(prefs.edit().putLong("last_id", next).commit())
        return next
    }
    companion object {
        fun key(json: JSONObject) = json.getString("ltk").chunked(2).map { it.toInt(16).toByte() }.toByteArray()
    }
}
