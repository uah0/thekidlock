package com.blelock.app

import org.json.JSONObject
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL

/** Uses the existing local ESP32 Agent API for the always-open UI control. */
object WifiLockControl {
    data class Result(val holdOpen: Boolean)

    fun toggle(ip: String): Result {
        require(ip.matches(Regex("\\d{1,3}(\\.\\d{1,3}){3}"))) { "IP ESP32 не задан" }
        val base = "http://$ip"
        val current = request("$base/api/agent/status", "GET")
        val next = !current.getBoolean("hold_open")
        val command = if (next) "always_open" else "auto"
        request("$base/api/agent/command?action=$command", "POST")

        repeat(10) {
            val confirmed = request("$base/api/agent/status", "GET").getBoolean("hold_open")
            if (confirmed == next) return Result(confirmed)
            Thread.sleep(200)
        }
        throw IOException("ESP32 не подтвердила переключение режима")
    }

    private fun request(address: String, method: String): JSONObject {
        val connection = URL(address).openConnection() as HttpURLConnection
        try {
            connection.requestMethod = method
            connection.connectTimeout = 2500
            connection.readTimeout = 2500
            connection.useCaches = false
            val status = connection.responseCode
            val stream = if (status in 200..299) connection.inputStream else connection.errorStream
            val body = stream?.bufferedReader(Charsets.UTF_8)?.use { it.readText() }.orEmpty()
            if (status !in 200..299) throw IOException("ESP32 HTTP $status: $body")
            return JSONObject(body)
        } finally {
            connection.disconnect()
        }
    }
}
