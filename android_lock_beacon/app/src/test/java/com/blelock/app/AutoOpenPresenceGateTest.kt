package com.blelock.app

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class AutoOpenPresenceGateTest {
    @Test
    fun lockedStatusBeforeOpenDoesNotRearm() {
        val gate = AutoOpenPresenceGate()
        gate.onOpenCommandAccepted()

        assertFalse(gate.onLockStatus("LOCKED"))
    }

    @Test
    fun confirmedOpenThenLockedRearmsExactlyOnce() {
        val gate = AutoOpenPresenceGate()
        gate.onOpenCommandAccepted()

        assertFalse(gate.onLockStatus("OPEN"))
        assertTrue(gate.onLockStatus("LOCKED"))
        assertFalse(gate.onLockStatus("LOCKED"))
    }

    @Test
    fun statusNamesAreNormalized() {
        val gate = AutoOpenPresenceGate()
        gate.onOpenCommandAccepted()

        assertFalse(gate.onLockStatus(" open "))
        assertTrue(gate.onLockStatus("locked"))
    }

    @Test
    fun unrelatedStatusDoesNotConfirmCycle() {
        val gate = AutoOpenPresenceGate()
        gate.onOpenCommandAccepted()

        assertFalse(gate.onLockStatus("OPEN_HOLD"))
        assertFalse(gate.onLockStatus("ERROR"))
        assertFalse(gate.onLockStatus("LOCKED"))
    }
}
