package com.blelock.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class WorkingModeReducerTest {
    @Test
    fun allFourAutoAndHoldOpenCombinationsAreRepresentable() {
        for (auto in listOf(false, true)) {
            for (hold in listOf(false, true)) {
                val state = WorkingModeState(autoOpenEnabled = auto, alwaysOpenEnabled = hold)
                val ui = WorkingModeUi.compute(state, alwaysOpenAvailable = true)
                assertEquals(auto, ui.autoSelected)
                assertEquals(hold, ui.alwaysOpenSelected)
                assertTrue(ui.autoEnabled)
            }
        }
    }

    @Test
    fun togglingAutoDoesNotChangeAlwaysOpen() {
        val start = WorkingModeState(autoOpenEnabled = false, alwaysOpenEnabled = true)
        val next = WorkingModeReducer.reduce(start, WorkingModeEvent.AutoOpenChanged(true))
        assertTrue(next.autoOpenEnabled)
        assertTrue(next.alwaysOpenEnabled)
    }

    @Test
    fun confirmingAlwaysOpenDoesNotChangeAuto() {
        val start = WorkingModeState(autoOpenEnabled = true, alwaysOpenEnabled = false)
        val next = WorkingModeReducer.reduce(start, WorkingModeEvent.AlwaysOpenConfirmed(true))
        assertTrue(next.autoOpenEnabled)
        assertTrue(next.alwaysOpenEnabled)
    }

    @Test
    fun autoButtonDoesNotDependOnWifiOrBleAvailability() {
        val ui = WorkingModeUi.compute(WorkingModeState(), alwaysOpenAvailable = false)
        assertTrue(ui.autoEnabled)
        assertFalse(ui.alwaysOpenEnabled)
    }
}
