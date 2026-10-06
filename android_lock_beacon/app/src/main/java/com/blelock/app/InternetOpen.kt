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
    fun start(context: Context, onEvent: (DoorActionEvent) -> Unit, done: () -> Unit): Boolean {
        if (!busy.compareAndSet(false, true)) return false
        val app = context.applicationContext
        Thread {
            var client: MqttClient? = null
            var key: ByteArray? = null
            try {
                val access = InternetAccess(app)
                val cfg = access.load() ?: error("Сначала подключите телефон к замку по Bluetooth")
                check(cfg.getBoolean("enabled") && cfg.getString("host").isNotEmpty()) {
                    "Включите MQTT в настройках замка и обновите параметры через Bluetooth"
                }
                val target = cfg.getString("target")
                val phone = cfg.getString("phone")
                val signingKey = InternetAccess.key(cfg); key = signingKey
                val prefix = "locks/" + target.replace(":", "")
                val replies = LinkedBlockingQueue<ByteArray>(16)
                client = MqttClient("tcp://" + cfg.getString("host") + ":" + MQTT_PORT,
                    "phone-" + UUID.randomUUID().toString(), MemoryPersistence()).apply {
                    timeToWait = 10_000
                    connect(MqttConnectOptions().apply {
                        connectionTimeout = 10
                        keepAliveInterval = 30
                        isCleanSession = true
                    })
                    subscribe("$prefix/result", 1) { _, message ->
                        if (!message.isRetained && message.payload.size <= 384) replies.offer(message.payload.clone())
                    }
                }
                val id = access.nextId()
                val time = System.currentTimeMillis() / 1000
                val tag = InternetAuth.sign(signingKey, InternetAuth.command(target, phone, time, id))
                val request = JSONObject().put("phone", phone).put("time", time.toString())
                    .put("id", id.toString()).put("mac", tag).toString().toByteArray(Charsets.UTF_8)
                client.publish("$prefix/command", request, 1, false)
                onEvent(DoorActionEvent.BrokerAccepted)
                val deadline = android.os.SystemClock.elapsedRealtime() + 90_000
                var completed = false
                var lastResult = ""
                while (android.os.SystemClock.elapsedRealtime() < deadline) {
                    val bytes = replies.poll(1, TimeUnit.SECONDS) ?: continue
                    val reply = runCatching { JSONObject(bytes.toString(Charsets.UTF_8)) }.getOrNull() ?: continue
                    if (reply.length() != 6 || reply.optString("phone") != phone ||
                        reply.optString("id") != id.toString()) continue
                    val stampString = reply.optString("time")
                    val stamp = stampString.toLongOrNull() ?: continue
                    if (stamp.toString() != stampString || stamp < time - 5 ||
                        !InternetAuth.fresh(stamp, System.currentTimeMillis() / 1000)) continue
                    val result = reply.optString("result")
                    if (result !in listOf("opened", "closing", "closed", "error", "interrupted")) continue
                    val faults = reply.optLong("faults", -1)
                    if (faults !in 0..4294967295L || !InternetAuth.verify(signingKey,
                            InternetAuth.result(target, phone, stamp, id, result, faults), reply.optString("mac"))) continue
                    if (result == lastResult) continue
                    lastResult = result
                    when (result) {
                        "opened" -> onEvent(DoorActionEvent.LockUnlocked(healthy = faults == 0L))
                        "closing" -> onEvent(DoorActionEvent.Closing)
                        "closed" -> {
                            onEvent(DoorActionEvent.LockClosed(healthy = faults == 0L))
                            completed = true
                        }
                        else -> {
                            onEvent(DoorActionEvent.Failed("Замок сообщил об ошибке выполнения"))
                            completed = true
                        }
                    }
                    if (completed) break
                }
                if (!completed) onEvent(DoorActionEvent.Failed("Нет подтверждения завершения. Проверьте состояние замка"))
            } catch (e: Exception) {
                onEvent(DoorActionEvent.Failed(when (e) {
                    is IllegalStateException -> e.message ?: "Нет настроек интернет-открытия"
                    else -> "Не удалось выполнить интернет-открытие. Проверьте связь и настройки"
                }))
            } finally {
                key?.fill(0)
                runCatching { client?.disconnect(1000) }
                runCatching { client?.close() }
                busy.set(false); done()
            }
        }.apply { name = "internet-open"; start() }
        return true
    }
}
