package com.blelock.app

import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec
import java.security.MessageDigest

/** Canonical HMAC input shared with ESP. The only command is implicit "open". */
object InternetAuth {
    fun command(target: String, phone: String, time: Long, id: Long) =
        "BLELOCK1|command|$target|$phone|$time|$id|open"
    fun result(target: String, phone: String, time: Long, id: Long, result: String, faults: Long) =
        "BLELOCK1|result|$target|$phone|$time|$id|$result|$faults"
    fun sign(key: ByteArray, text: String): String {
        require(key.size == 16)
        return Mac.getInstance("HmacSHA256").run {
            init(SecretKeySpec(key, "HmacSHA256"))
            doFinal(text.toByteArray(Charsets.UTF_8)).joinToString("") { "%02x".format(it.toInt() and 255) }
        }
    }
    fun verify(key: ByteArray, text: String, tag: String): Boolean =
        tag.matches(Regex("[0-9a-f]{64}")) && MessageDigest.isEqual(
            sign(key, text).toByteArray(Charsets.US_ASCII), tag.toByteArray(Charsets.US_ASCII))
    fun fresh(issued: Long, now: Long) = issued >= now - 30 && issued <= now + 5
}
