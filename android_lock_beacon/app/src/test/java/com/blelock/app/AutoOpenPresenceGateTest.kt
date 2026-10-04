package com.blelock.app

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class AutoOpenPresenceGateTest {
    @Test
    fun openingBlocksAnotherAutomaticOpen() {
        val gate = AutoOpenPresenceGate()
        gate.onOpenCommandAccepted()

        assertTrue(gate.isBlocked)
    }

    @Test
    fun onlyConfirmedDepartureRearms() {
        val gate = AutoOpenPresenceGate()
        gate.onOpenCommandAccepted()

        assertTrue(gate.isBlocked)
        gate.onDepartureConfirmed()
        assertFalse(gate.isBlocked)
    }
}
