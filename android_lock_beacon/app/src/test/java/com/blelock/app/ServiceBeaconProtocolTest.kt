package com.blelock.app

import org.junit.Assert.*
import org.junit.Test

class ServiceBeaconProtocolTest {
    private val id = 0x89abcdef.toInt()
    private fun response(code: Int = 0, hold: Boolean = true, requestId: Int = id) =
        byteArrayOf(1, (requestId ushr 24).toByte(), (requestId ushr 16).toByte(),
            (requestId ushr 8).toByte(), requestId.toByte(), code.toByte(), if (hold) 1 else 0)

    @Test fun independentRequestsAndCombinedWifiIp() {
        assertArrayEquals(byteArrayOf(1, 1, 0, 0, 0, 0, 0), ServiceBeaconProtocol.request(true, false, null))
        assertArrayEquals(byteArrayOf(1, 2, 0, 0, 0, 0, 0), ServiceBeaconProtocol.request(false, true, null))
        assertArrayEquals(byteArrayOf(1, 3, 0, 0, 0, 0, 0), ServiceBeaconProtocol.request(true, true, null))
    }

    @Test fun queryAndExplicitSetKeepRequestIdentity() {
        val query = ServiceBeaconProtocol.request(false, false, ServiceBeaconProtocol.HoldRequest(id, null))
        assertEquals(4, query[1].toInt())
        assertEquals(0, query[2].toInt())
        assertEquals(1, ServiceBeaconProtocol.request(false, false, ServiceBeaconProtocol.HoldRequest(id, true))[2].toInt())
        assertEquals(2, ServiceBeaconProtocol.request(false, false, ServiceBeaconProtocol.HoldRequest(id, false))[2].toInt())
        assertEquals(id, ServiceBeaconProtocol.result(response(), ServiceBeaconProtocol.HoldRequest(id, true))?.id)
    }

    @Test fun homeWifiRequestDoesNotSetHoldAndKeepsIdentity() {
        assertArrayEquals(byteArrayOf(1, 8, 0, 0x89.toByte(), 0xab.toByte(), 0xcd.toByte(), 0xef.toByte()),
            ServiceBeaconProtocol.request(false, false, null, id))
        assertNotNull(ServiceBeaconProtocol.result(response(hold = false), ServiceBeaconProtocol.HoldRequest(id, null)))
        assertNull(ServiceBeaconProtocol.result(response(requestId = id + 1), ServiceBeaconProtocol.HoldRequest(id, null)))
    }

    @Test(expected = IllegalArgumentException::class) fun homeRequestCannotProvisionPhoneWifi() {
        ServiceBeaconProtocol.request(true, false, null, id)
    }

    @Test(expected = IllegalArgumentException::class) fun homeRequestCannotChangeHoldMode() {
        ServiceBeaconProtocol.request(false, false, ServiceBeaconProtocol.HoldRequest(id, true), id)
    }

    @Test fun staleOrUnsolicitedResultIsRejected() {
        assertNull(ServiceBeaconProtocol.result(response(), null))
        assertNull(ServiceBeaconProtocol.result(response(requestId = id + 1), ServiceBeaconProtocol.HoldRequest(id, true)))
    }

    @Test fun malformedResultIsRejected() {
        val request = ServiceBeaconProtocol.HoldRequest(id, true)
        assertNull(ServiceBeaconProtocol.result(response().copyOf(6), request))
        assertNull(ServiceBeaconProtocol.result(response().copyOf(8), request))
        assertNull(ServiceBeaconProtocol.result(response().also { it[0] = 2 }, request))
        assertNull(ServiceBeaconProtocol.result(response(code = 4), request))
        assertNull(ServiceBeaconProtocol.result(response().also { it[6] = 2 }, request))
    }

    @Test fun successMustConfirmDesiredStateButQueryAcceptsEitherState() {
        assertNull(ServiceBeaconProtocol.result(response(hold = false), ServiceBeaconProtocol.HoldRequest(id, true)))
        assertNotNull(ServiceBeaconProtocol.result(response(hold = false), ServiceBeaconProtocol.HoldRequest(id, null)))
        assertNotNull(ServiceBeaconProtocol.result(response(code = 1, hold = false), ServiceBeaconProtocol.HoldRequest(id, true)))
    }

    @Test(expected = IllegalArgumentException::class) fun zeroRequestIdIsRejected() {
        ServiceBeaconProtocol.HoldRequest(0, true)
    }
}
