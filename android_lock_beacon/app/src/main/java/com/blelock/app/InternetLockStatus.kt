package com.blelock.app

import android.content.Context
import org.eclipse.paho.client.mqttv3.MqttClient
import org.eclipse.paho.client.mqttv3.MqttConnectOptions
import org.eclipse.paho.client.mqttv3.persist.MemoryPersistence
import org.json.JSONObject
import java.util.UUID
import java.util.concurrent.atomic.AtomicInteger
import kotlin.math.abs

/** Foreground-only owner subscription. A retained status is accepted only while fresh. */
object InternetLockStatus {
    data class Snapshot(val state: String, val door: String, val faults: Long,
                        val publishedAtSeconds: Long, val receivedAtMs: Long)

    private val generation = AtomicInteger()

    fun start(context: Context, onStatus: (Snapshot?) -> Unit) {
        val token = generation.incrementAndGet()
        val app = context.applicationContext
        Thread({
            val access = InternetAccess(app)
            while (generation.get() == token) {
                var client: MqttClient? = null
                var key: ByteArray? = null
                try {
                    val cfg = access.load()
                    if (cfg == null || !cfg.optBoolean("enabled") || cfg.optString("host").isBlank()) {
                        onStatus(null)
                        Thread.sleep(3000)
                        continue
                    }
                    val configText = cfg.toString()
                    val host = cfg.getString("host")
                    val target = cfg.getString("target")
                    val phone = cfg.getString("phone")
                    val prefix = "j" + target.replace(":", "")
                    val ownerKey = InternetAccess.key(cfg)
                    key = ownerKey
                    val mqtt = MqttClient("tcp://$host:1883", "phone-status-${UUID.randomUUID()}", MemoryPersistence())
                    client = mqtt
                    mqtt.timeToWait = 10_000
                    mqtt.connect(MqttConnectOptions().apply {
                        connectionTimeout = 10
                        keepAliveInterval = 25
                        isCleanSession = true
                    })
                    if (generation.get() != token) break
                    mqtt.subscribe("$prefix/4sR", 1) { _, message ->
                        if (generation.get() != token || message.payload.size > 1024) return@subscribe
                        val plain = InternetCrypto.open(ownerKey, "state", target, phone, message.payload)
                            ?: return@subscribe
                        val snapshot = runCatching {
                            val json = JSONObject(plain.toString(Charsets.UTF_8))
                            check(json.length() == 4)
                            val stamp = json.getString("time").toLong()
                            val state = json.getString("state")
                            check(state == "offline" || abs(System.currentTimeMillis() / 1000 - stamp) <= 15)
                            val door = json.getString("door")
                            val faults = json.getLong("faults")
                            check(state in setOf("locked", "unlocked", "fault", "unknown", "offline"))
                            check(door in setOf("open", "closed", "unknown"))
                            check(faults in 0..4294967295L)
                            Snapshot(state, door, faults, stamp, System.currentTimeMillis())
                        }.getOrNull()
                        plain.fill(0)
                        if (snapshot != null && generation.get() == token) onStatus(snapshot)
                    }
                    while (generation.get() == token && mqtt.isConnected && access.load()?.toString() == configText)
                        Thread.sleep(1000)
                } catch (_: Exception) {
                    if (generation.get() == token) onStatus(null)
                } finally {
                    if (generation.get() == token) onStatus(null)
                    runCatching { client?.disconnect(1000) }
                    runCatching { client?.close() }
                    key?.fill(0)
                }
                if (generation.get() == token) Thread.sleep(3000)
            }
        }, "internet-lock-status").apply { isDaemon = true; start() }
    }

    fun stop() { generation.incrementAndGet() }
}
