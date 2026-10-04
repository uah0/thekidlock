package com.blelock.app

import android.util.Log
import java.net.Inet4Address
import java.net.NetworkInterface

/** Best-effort lookup of the active LocalOnlyHotspot interface without fixed subnet assumptions. */
internal object HotspotIpv4Resolver {
    private const val TAG = "HotspotIpv4Resolver"

    data class Candidate(val interfaceName: String, val address: Inet4Address)

    fun resolve(allowWlanFallback: Boolean, logResult: Boolean = true): Inet4Address? {
        val candidates = runCatching {
            NetworkInterface.getNetworkInterfaces()?.toList().orEmpty().flatMap { networkInterface ->
                if (!networkInterface.isUp || networkInterface.isLoopback || networkInterface.isPointToPoint) {
                    emptyList()
                } else {
                    networkInterface.interfaceAddresses.mapNotNull { interfaceAddress ->
                        val address = interfaceAddress.address as? Inet4Address ?: return@mapNotNull null
                        val broadcast = interfaceAddress.broadcast as? Inet4Address
                        if (!isUsableHotspotAddress(address) ||
                            (broadcast != null && broadcast.address.contentEquals(address.address))) null
                        else Candidate(networkInterface.name, address)
                    }
                }
            }
        }.onFailure {
            if (logResult) Log.w(TAG, "Не удалось перечислить IP интерфейсов", it)
        }.getOrDefault(emptyList())
        val selected = select(candidates, allowWlanFallback)
        if (logResult) {
            if (selected == null) {
                Log.w(TAG, "IPv4 hotspot не определён однозначно; кандидаты=${candidates.map { "${it.interfaceName}:${it.address.hostAddress}" }}")
            } else {
                Log.i(TAG, "IPv4 hotspot определён: ${selected.address.hostAddress} на ${selected.interfaceName}")
            }
        }
        return selected?.address
    }

    internal fun select(candidates: List<Candidate>, allowWlanFallback: Boolean = true): Candidate? {
        val apCandidates = candidates.filter { isAccessPointInterface(it.interfaceName) }
        val pool = if (apCandidates.isNotEmpty()) apCandidates else if (allowWlanFallback)
            candidates.filter { it.interfaceName.startsWith("wlan", ignoreCase = true) }
        else emptyList()
        return pool.distinctBy { it.address.hostAddress }.singleOrNull()
    }

    internal fun isUsableHotspotAddress(address: Inet4Address): Boolean {
        val bytes = address.address.map { it.toInt() and 0xff }
        if (bytes.size != 4 || bytes.all { it == 0 } || bytes.all { it == 255 }) return false
        if (bytes[0] == 127 || bytes[0] >= 224) return false
        return !address.isAnyLocalAddress && !address.isLoopbackAddress &&
            !address.isLinkLocalAddress && !address.isMulticastAddress
    }

    private fun isAccessPointInterface(name: String): Boolean =
        name.matches(Regex("(?i)(ap|softap|swlan)\\d*"))
}
