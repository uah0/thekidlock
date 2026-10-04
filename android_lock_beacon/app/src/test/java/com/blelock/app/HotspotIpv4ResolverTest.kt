package com.blelock.app

import java.net.Inet4Address
import java.net.InetAddress
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test

class HotspotIpv4ResolverTest {
    private fun address(value: String) = InetAddress.getByName(value) as Inet4Address

    @Test
    fun selectsApInterfaceOverOtherWifiInterfaces() {
        val ap = HotspotIpv4Resolver.Candidate("ap0", address("10.20.0.1"))
        val station = HotspotIpv4Resolver.Candidate("wlan0", address("192.168.1.20"))

        assertSame(ap, HotspotIpv4Resolver.select(listOf(station, ap)))
    }

    @Test
    fun doesNotGuessWhenWifiInterfaceIsAmbiguous() {
        val first = HotspotIpv4Resolver.Candidate("wlan0", address("192.168.1.20"))
        val second = HotspotIpv4Resolver.Candidate("wlan1", address("10.20.0.1"))

        assertNull(HotspotIpv4Resolver.select(listOf(first, second)))
    }

    @Test
    fun doesNotTreatStationWifiAsHotspotWhenApInterfaceIsUnknown() {
        val station = HotspotIpv4Resolver.Candidate("wlan0", address("192.168.1.20"))

        assertNull(HotspotIpv4Resolver.select(listOf(station), allowWlanFallback = false))
    }

    @Test
    fun rejectsUnavailableAndNonUnicastAddresses() {
        assertFalse(HotspotIpv4Resolver.isUsableHotspotAddress(address("0.0.0.0")))
        assertFalse(HotspotIpv4Resolver.isUsableHotspotAddress(address("127.0.0.1")))
        assertFalse(HotspotIpv4Resolver.isUsableHotspotAddress(address("224.0.0.1")))
        assertFalse(HotspotIpv4Resolver.isUsableHotspotAddress(address("255.255.255.255")))
        assertTrue(HotspotIpv4Resolver.isUsableHotspotAddress(address("10.20.0.1")))
        assertEquals("10.20.0.1", address("10.20.0.1").hostAddress)
    }
}
