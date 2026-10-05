package com.blelock.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class WorkingModeReducerTest {
    @Test
    fun commandConfirmationDoesNotChangeAutoSelection() {
        for (auto in listOf(false, true)) {
            for (confirmed in listOf(false, true)) {
                val state = WorkingModeState(autoOpenEnabled = auto, holdCommandConfirmed = confirmed)
                val ui = WorkingModeUi.compute(state, alwaysOpenAvailable = true)
                assertEquals(auto, ui.autoSelected)
                assertEquals(confirmed, ui.alwaysOpenSelected)
                assertTrue(ui.autoEnabled)
            }
        }
    }

    @Test
    fun togglingAutoDoesNotChangeCommandConfirmation() {
        val start = WorkingModeState(autoOpenEnabled = false, holdCommandConfirmed = true)
        val next = WorkingModeReducer.reduce(start, WorkingModeEvent.AutoOpenChanged(true))
        assertTrue(next.autoOpenEnabled)
        assertTrue(next.holdCommandConfirmed)
    }

    @Test
    fun confirmingCommandDoesNotChangeAuto() {
        val start = WorkingModeState(autoOpenEnabled = true)
        val next = WorkingModeReducer.reduce(start, WorkingModeEvent.HoldCommandResult(true))
        assertTrue(next.autoOpenEnabled)
        assertTrue(next.holdCommandConfirmed)
    }

    @Test
    fun failedCommandClearsPreviousConfirmation() {
        val start = WorkingModeState(holdCommandConfirmed = true)
        val next = WorkingModeReducer.reduce(start, WorkingModeEvent.HoldCommandResult(false))
        assertFalse(WorkingModeUi.compute(next, true).alwaysOpenSelected)
    }

    @Test
    fun freshScreenDoesNotRestorePreviousCommandConfirmation() {
        assertFalse(WorkingModeUi.compute(WorkingModeState(), true).alwaysOpenSelected)
    }

    @Test
    fun autoButtonDoesNotDependOnWifiOrBleAvailability() {
        val ui = WorkingModeUi.compute(WorkingModeState(), alwaysOpenAvailable = false)
        assertTrue(ui.autoEnabled)
        assertFalse(ui.alwaysOpenEnabled)
    }
}
