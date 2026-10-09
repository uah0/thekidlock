package com.blelock.app

import java.net.URI

internal class WebUiRequestPolicy(origin: String) {
    private val base = URI(origin)

    init {
        require(base.scheme == "http" && base.userInfo == null && base.port == -1)
        val octets = base.host.orEmpty().split('.')
        require(octets.size == 4 && octets.all {
            it.isNotEmpty() && it.all(Char::isDigit) && (it.toIntOrNull() ?: -1) in 0..255
        })
    }

    fun allows(url: String): Boolean = runCatching {
        val candidate = URI(url)
        candidate.scheme == base.scheme && candidate.host == base.host &&
            candidate.port == base.port && candidate.userInfo == null
    }.getOrDefault(false)

    fun isFirmwareRequest(url: String, method: String): Boolean = allows(url) && runCatching {
        val candidate = URI(url)
        method == "GET" && candidate.rawPath == FIRMWARE_PATH &&
            candidate.rawQuery == null && candidate.rawFragment == null
    }.getOrDefault(false)

    fun isCalibrationPage(url: String): Boolean = allows(url) && runCatching {
        URI(url).rawPath == "/calibration"
    }.getOrDefault(false)

    companion object {
        const val FIRMWARE_PATH = "/__app/firmware"
    }
}
