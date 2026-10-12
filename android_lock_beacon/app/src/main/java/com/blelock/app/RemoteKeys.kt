package com.blelock.app

import android.content.Context
import android.util.Base64
import org.eclipse.paho.client.mqttv3.MqttClient
import org.eclipse.paho.client.mqttv3.MqttConnectOptions
import org.eclipse.paho.client.mqttv3.persist.MemoryPersistence
import org.json.JSONObject
import java.util.UUID
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit

/** Owner-only remote bond management over the existing authenticated MQTT envelope. */
object RemoteKeys {
    data class Phone(val address: String, val type: Int, val name: String)
    data class Snapshot(val token: String, val phones: List<Phone>)
    private const val PORT = 1883
    private const val TAG = "RemoteKeys"
    // Log.println is used only for this temporary targeted field diagnostic; the
    // global release rule intentionally strips the usual Log.i/w calls.
    private fun trace(message: String) { android.util.Log.println(android.util.Log.ASSERT, TAG, message) }

    fun fetch(context: Context, done: (Result<Snapshot>) -> Unit) = run(context, done) { access, cfg, key, client, replies ->
        var token = ""
        var total = -1
        val phones = mutableListOf<Phone>()
        do {
            val reply = query(context, access, cfg, key, client, replies, "list", phones.size, token, "")
            check(reply.getString("status") == "ok") { context.getString(R.string.keys_list_changed) }
            val count = reply.getInt("count")
            check(count in 0..20 && (total == -1 || total == count) && reply.getInt("index") == phones.size)
            total = count
            val nextToken = reply.getString("token")
            check(nextToken.matches(Regex("[0-9a-f]{16}")) && (token.isEmpty() || token == nextToken))
            token = nextToken
            if (count == 0) {
                check(phones.isEmpty() && reply.getInt("index") == 0)
                return@run Snapshot(token, emptyList())
            }
            val address = reply.getString("addr")
            val type = reply.getInt("type")
            check(address.matches(Regex("[0-9A-F]{2}(:[0-9A-F]{2}){5}")) && type in 0..1)
            val name = String(Base64.decode(reply.getString("name"), Base64.NO_WRAP), Charsets.UTF_8)
            check(name.toByteArray(Charsets.UTF_8).size <= 48)
            phones += Phone(address, type, name)
        } while (phones.size < total)
        Snapshot(token, phones)
    }

    fun apply(context: Context, snapshot: Snapshot, selected: Set<Int>, done: (Result<Int>) -> Unit) =
        run(context, done) { access, cfg, key, client, replies ->
            check(snapshot.phones.size in 1..20 && selected.isNotEmpty() &&
                selected.all { it in snapshot.phones.indices })
            val mask = snapshot.phones.indices.joinToString("") { if (it in selected) "1" else "0" }
            val reply = query(context, access, cfg, key, client, replies, "apply", -1, snapshot.token, mask)
            if (reply.getString("status") == "partial")
                error(context.getString(R.string.revoke_keys_partial, reply.getInt("deleted")))
            check(reply.getString("status") == "ok") { context.getString(R.string.keys_list_changed) }
            val deleted = reply.getInt("deleted")
            check(deleted == selected.size)
            deleted
        }

    private fun <T> run(context: Context, done: (Result<T>) -> Unit,
                        action: (InternetAccess, JSONObject, ByteArray, MqttClient, LinkedBlockingQueue<ByteArray>) -> T) {
        Thread {
            var client: MqttClient? = null
            var key: ByteArray? = null
            val result = runCatching {
                val app = context.applicationContext
                val access = InternetAccess(app)
                val cfg = access.load() ?: error(app.getString(R.string.internet_setup_required))
                check(cfg.getBoolean("enabled") && cfg.getString("host").isNotEmpty()) {
                    app.getString(R.string.internet_mqtt_required)
                }
                val signingKey = InternetAccess.key(cfg)
                key = signingKey
                val replies = LinkedBlockingQueue<ByteArray>(32)
                val prefix = "j" + cfg.getString("target").replace(":", "")
                trace("connecting broker=${cfg.getString("host")} prefix=$prefix")
                val mqtt = MqttClient("tcp://${cfg.getString("host")}:$PORT",
                    "phone-" + UUID.randomUUID(), MemoryPersistence()).apply {
                    timeToWait = 10_000
                    connect(MqttConnectOptions().apply {
                        connectionTimeout = 10
                        keepAliveInterval = 30
                        isCleanSession = true
                    })
                    subscribe("$prefix/2bL", 1) { _, message ->
                        trace("reply received bytes=${message.payload.size} retained=${message.isRetained}")
                        if (!message.isRetained && message.payload.size <= 1024) replies.offer(message.payload.clone())
                    }
                }
                trace("reply subscription ready")
                client = mqtt
                action(access, cfg, signingKey, mqtt, replies)
            }
            key?.fill(0)
            result.exceptionOrNull()?.let { trace("operation failed: ${it.javaClass.simpleName}: ${it.message}") }
            runCatching { client?.disconnect(1000) }
            runCatching { client?.close() }
            (context as? android.app.Activity)?.runOnUiThread { done(result) }
        }.apply { name = "remote-keys"; start() }
    }

    private fun query(context: Context, access: InternetAccess, cfg: JSONObject, key: ByteArray, client: MqttClient,
                      replies: LinkedBlockingQueue<ByteArray>, op: String, index: Int,
                      token: String, mask: String): JSONObject {
        val target = cfg.getString("target")
        val phone = cfg.getString("phone")
        val prefix = "j" + target.replace(":", "")
        val id = access.nextId().toString()
        val time = System.currentTimeMillis() / 1000
        val request = JSONObject().put("phone", phone).put("time", time.toString()).put("id", id)
            .put("op", op).put("index", index).put("token", token).put("mask", mask)
            .toString().toByteArray(Charsets.UTF_8)
        val sealed = InternetCrypto.seal(key, "admin-request", target, phone, request)
        trace("publishing op=$op index=$index bytes=${sealed.size}")
        client.getTopic("$prefix/5mN").publish(sealed, 1, false).waitForCompletion(10_000)
        trace("broker accepted op=$op index=$index")
        val deadline = android.os.SystemClock.elapsedRealtime() + 15_000
        while (android.os.SystemClock.elapsedRealtime() < deadline) {
            val bytes = replies.poll(1, TimeUnit.SECONDS) ?: continue
            val plain = InternetCrypto.open(key, "admin-reply", target, phone, bytes)
            if (plain == null) { trace("reply decrypt rejected bytes=${bytes.size}"); continue }
            val reply = runCatching { JSONObject(String(plain, Charsets.UTF_8)) }.getOrNull() ?: continue
            val stamp = reply.optString("time").toLongOrNull() ?: continue
            if (reply.length() != 11 || reply.optString("id") != id || reply.optString("op") != op ||
                stamp < time - 5 || !InternetAuth.fresh(stamp, System.currentTimeMillis() / 1000)) {
                trace("reply metadata rejected op=$op fields=${reply.length()}")
                continue
            }
            trace("reply accepted op=$op index=$index status=${reply.optString("status")}")
            return reply
        }
        error(context.getString(R.string.revoke_keys_timeout))
    }
}
