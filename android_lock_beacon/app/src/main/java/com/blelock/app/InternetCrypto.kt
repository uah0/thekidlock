package com.blelock.app

import org.bouncycastle.crypto.digests.SHA256Digest
import org.bouncycastle.crypto.generators.HKDFBytesGenerator
import org.bouncycastle.crypto.params.HKDFParameters
import org.json.JSONObject
import java.security.SecureRandom
import javax.crypto.Cipher
import javax.crypto.spec.GCMParameterSpec
import javax.crypto.spec.SecretKeySpec

/** MQTT envelope v2. The BLE LTK never travels through MQTT. */
object InternetCrypto {
    private val random = SecureRandom()
    private val salt = "BLELOCK2|mqtt".toByteArray(Charsets.US_ASCII)
    private fun context(direction: String, target: String, phone: String) =
        "BLELOCK2|$direction|$target|$phone".toByteArray(Charsets.US_ASCII)

    fun deriveKey(ltk: ByteArray, direction: String, target: String, phone: String): ByteArray {
        require(ltk.size == 16 && phone.matches(Regex("[01][0-9a-f]{12}")))
        val generator = HKDFBytesGenerator(SHA256Digest())
        generator.init(HKDFParameters(ltk, salt, context(direction, target, phone)))
        return ByteArray(16).also { generator.generateBytes(it, 0, it.size) }
    }

    fun seal(ltk: ByteArray, direction: String, target: String, phone: String, plain: ByteArray): ByteArray {
        require(plain.isNotEmpty() && plain.size <= 352)
        val key = deriveKey(ltk, direction, target, phone)
        val nonce = ByteArray(12).also(random::nextBytes)
        return try {
            val cipher = Cipher.getInstance("AES/GCM/NoPadding")
            cipher.init(Cipher.ENCRYPT_MODE, SecretKeySpec(key, "AES"), GCMParameterSpec(128, nonce))
            cipher.updateAAD(context(direction, target, phone))
            JSONObject().put("v", 2)
                .put("nonce", nonce.toHex()).put("data", cipher.doFinal(plain).toHex())
                .toString().toByteArray(Charsets.UTF_8)
        } finally { key.fill(0) }
    }

    fun open(ltk: ByteArray, direction: String, target: String, phone: String, envelope: ByteArray): ByteArray? =
        runCatching {
            require(envelope.size <= 1024)
            val json = JSONObject(envelope.toString(Charsets.UTF_8))
            require(json.length() == 3 && json.getInt("v") == 2)
            val nonce = json.getString("nonce").hexBytes()
            val data = json.getString("data").hexBytes()
            require(nonce.size == 12 && data.size in 17..368)
            val key = deriveKey(ltk, direction, target, phone)
            try {
                val cipher = Cipher.getInstance("AES/GCM/NoPadding")
                cipher.init(Cipher.DECRYPT_MODE, SecretKeySpec(key, "AES"), GCMParameterSpec(128, nonce))
                cipher.updateAAD(context(direction, target, phone))
                cipher.doFinal(data)
            } finally { key.fill(0) }
        }.getOrNull()

    private fun ByteArray.toHex() = joinToString("") { "%02x".format(it.toInt() and 255) }
    private fun String.hexBytes(): ByteArray {
        require(length % 2 == 0 && all { it in '0'..'9' || it in 'a'..'f' })
        return chunked(2).map { it.toInt(16).toByte() }.toByteArray()
    }
}
