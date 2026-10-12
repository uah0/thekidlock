package com.blelock.app

import android.content.Context
import org.eclipse.paho.client.mqttv3.MqttClient
import org.eclipse.paho.client.mqttv3.MqttConnectOptions
import org.eclipse.paho.client.mqttv3.persist.MemoryPersistence
import org.json.JSONObject
import java.util.UUID
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

/** Each tap opens a bounded MQTT session. PUBACK is never shown as lock execution. */
object InternetOpen {
    private const val MQTT_PORT = 1883
    private val busy = AtomicBoolean(false)
    fun start(context: Context, progress: (InternetOpenStage, String?, Long?) -> Unit, onError: (String) -> Unit, done: () -> Unit): Boolean {
        if (!busy.compareAndSet(false, true)) return false
        val app = context.applicationContext
        Thread {
            var client: MqttClient? = null
            var key: ByteArray? = null
            try {
                val access = InternetAccess(app)
                val cfg = access.load() ?: error(app.getString(R.string.internet_setup_required))
                check(cfg.getBoolean("enabled") && cfg.getString("host").isNotEmpty()) {
                    app.getString(R.string.internet_mqtt_required)
                }
                val target = cfg.getString("target")
                val phone = cfg.getString("phone")
                val signingKey = InternetAccess.key(cfg); key = signingKey
                val prefix = "j" + target.replace(":", "")
                val replies = LinkedBlockingQueue<ByteArray>(16)
                progress(InternetOpenStage.CONNECTING, null, null)
                client = MqttClient("tcp://" + cfg.getString("host") + ":" + MQTT_PORT,
                    "phone-" + UUID.randomUUID().toString(), MemoryPersistence()).apply {
                    timeToWait = 10_000
                    connect(MqttConnectOptions().apply {
                        connectionTimeout = 10
                        keepAliveInterval = 30
                        isCleanSession = true
                    })
                    subscribe("$prefix/7qK", 1) { _, message ->
                        if (!message.isRetained && message.payload.size <= 1024) replies.offer(message.payload.clone())
                    }
                }
                val id = access.nextId()
                val time = System.currentTimeMillis() / 1000
                val tag = InternetAuth.sign(signingKey, InternetAuth.command(target, phone, time, id))
                val request = JSONObject().put("phone", phone).put("time", time.toString())
                    .put("id", id.toString()).put("mac", tag).toString().toByteArray(Charsets.UTF_8)
                val sealedRequest = InternetCrypto.seal(signingKey, "command", target, phone, request)
                val delivery = client.getTopic("$prefix/0yF").publish(sealedRequest, 1, false)
                progress(InternetOpenStage.COMMAND_SENT, null, null)
                delivery.waitForCompletion(10_000)
                progress(InternetOpenStage.BROKER_ACCEPTED, null, null)
                val deadline = android.os.SystemClock.elapsedRealtime() + 90_000
                var completed = false
                var lastResult = ""
                var lockWasOpened = false
                var doorWasOpened = false
                while (android.os.SystemClock.elapsedRealtime() < deadline) {
                    val bytes = replies.poll(1, TimeUnit.SECONDS) ?: continue
                    val plain = InternetCrypto.open(signingKey, "result", target, phone, bytes) ?: continue
                    val reply = try { JSONObject(plain.toString(Charsets.UTF_8)) } catch (_: Exception) { continue }
                    val hasDoorState = reply.has("door")
                    val hasCloseRemaining = reply.has("close_remaining_ms")
                    val expectedFields = when {
                        hasDoorState && hasCloseRemaining -> 8
                        hasDoorState -> 7
                        else -> 6
                    }
                    if (reply.length() != expectedFields || reply.optString("phone") != phone ||
                        reply.optString("id") != id.toString()) continue
                    val stampString = reply.optString("time")
                    val stamp = stampString.toLongOrNull() ?: continue
                    if (stamp.toString() != stampString || stamp < time - 5 ||
                        !InternetAuth.fresh(stamp, System.currentTimeMillis() / 1000)) continue
                    val result = reply.optString("result")
                    if (result !in listOf("opened", "closing", "closed", "error", "interrupted")) continue
                    val door = if (hasDoorState) reply.optString("door") else "unknown"
                    if (door !in listOf("unknown", "open", "closed")) continue
                    val faults = reply.optLong("faults", -1)
                    val closeRemainingMs = if (hasCloseRemaining) reply.optLong("close_remaining_ms", -1) else null
                    if (closeRemainingMs != null && closeRemainingMs !in 0L..INTERNET_CLOSE_DELAY_MS) continue
                    if (faults !in 0..4294967295L || !InternetAuth.verify(signingKey,
                            InternetAuth.result(target, phone, stamp, id, result, faults,
                                if (hasDoorState) door else null, closeRemainingMs), reply.optString("mac"))) continue
                    val eventKey = "$result|$door|$faults"
                    if (eventKey == lastResult) continue
                    lastResult = eventKey
                    if (result == "opened" && faults == 0L) lockWasOpened = true
                    if (hasDoorState && door == "open") doorWasOpened = true
                    val stage = internetOpenStageForReply(result, faults, door, lockWasOpened, doorWasOpened)
                    progress(stage, if (hasDoorState) door else null, closeRemainingMs)
                    if (result == "closed" && door == "open") {
                        onError(app.getString(R.string.internet_door_still_open))
                        completed = true
                    } else if (stage == InternetOpenStage.ERROR) {
                        onError(if (faults != 0L) app.getString(R.string.lock_error) else app.getString(R.string.internet_execution_failed))
                        completed = true
                    } else if (stage == InternetOpenStage.CLOSED ||
                        stage == InternetOpenStage.CLOSED_WITHOUT_DOOR_OPEN) {
                        completed = true
                    }
                    if (completed) break
                }
                if (!completed) { progress(InternetOpenStage.ERROR, null, null); onError(app.getString(R.string.internet_completion_missing)) }
            } catch (e: Exception) {
                progress(InternetOpenStage.ERROR, null, null)
                onError(if (e is IllegalStateException) e.message ?: app.getString(R.string.internet_error) else app.getString(R.string.internet_connection_failed))
            } finally {
                key?.fill(0)
                runCatching { client?.disconnect(1000) }
                runCatching { client?.close() }
                busy.set(false); done()
            }
        }.apply { name = "internet-open"; start() }
        return true
    }

    private const val INTERNET_CLOSE_DELAY_MS = 20_000L
}
