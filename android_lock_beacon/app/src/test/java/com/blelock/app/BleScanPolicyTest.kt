package com.blelock.app

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BleScanPolicyTest {
    @Test
    fun autoOpenOffDoesNotArmPassiveScan() {
        assertFalse(BleScanPolicy.shouldRegister(false, true, true))
    }

    @Test
    fun passiveScanNeedsPermissionAndBluetooth() {
        assertFalse(BleScanPolicy.shouldRegister(true, false, true))
        assertFalse(BleScanPolicy.shouldRegister(true, true, false))
        assertTrue(BleScanPolicy.shouldRegister(true, true, true))
    }

    @Test
    fun onlyFreshAdvertisementWithLockServiceUuidIsAccepted() {
        assertTrue(
            BleScanPolicy.isFreshLockResult(
                1_000_000_000L,
                listOf(BleScanPolicy.SERVICE_UUID.uppercase()),
                -70,
                -75
            )
        )
        assertFalse(BleScanPolicy.isFreshLockResult(1_000_000_000L, emptyList(), -70, -75))
        assertFalse(
            BleScanPolicy.isFreshLockResult(
                1_000_000_000L,
                listOf("0000180f-0000-1000-8000-00805f9b34fb"),
                -70,
                -75
            )
        )
        assertFalse(
            BleScanPolicy.isFreshLockResult(
                BleScanPolicy.MAX_RESULT_AGE_NANOS + 1,
                listOf(BleScanPolicy.SERVICE_UUID),
                -70,
                -75
            )
        )
    }

    @Test
    fun rssiAboveThresholdIsAccepted() {
        assertTrue(
            BleScanPolicy.isFreshLockResult(
                1_000_000_000L, listOf(BleScanPolicy.SERVICE_UUID), -74, -75
            )
        )
    }

    @Test
    fun rssiExactlyAtThresholdIsAccepted() {
        assertTrue(
            BleScanPolicy.isFreshLockResult(
                1_000_000_000L, listOf(BleScanPolicy.SERVICE_UUID), -75, -75
            )
        )
    }

    @Test
    fun rssiBelowThresholdIsRejectedEvenForFreshLockAdvertisement() {
        assertFalse(
            BleScanPolicy.isFreshLockResult(
                1_000_000_000L, listOf(BleScanPolicy.SERVICE_UUID), -76, -75
            )
        )
    }

    @Test
    fun burstOfAdvertisementsStartsOneActiveFlowUntilReleased() {
        val gate = BleDetectionGate()
        val accepted = (1..10).count { gate.tryAcquire() }

        assertTrue(accepted == 1)
        gate.release()
        assertTrue(gate.tryAcquire())
    }

    @Test
    fun releasingGateAllowsNextDetectionAttempt() {
        val gate = BleDetectionGate()

        assertTrue(gate.tryAcquire())
        gate.release()
        assertTrue(gate.tryAcquire())
    }
}
