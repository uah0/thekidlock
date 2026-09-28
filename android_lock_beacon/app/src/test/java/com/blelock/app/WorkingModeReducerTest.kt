package com.blelock.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class WorkingModeReducerTest {
    private val readyUnknown = WorkingModeState(bleState = BleRepository.State.READY)

    private fun reduce(
        state: WorkingModeState,
        event: WorkingModeEvent,
        time: Long = 1L,
        autoOpenEnabled: Boolean = false
    ) = WorkingModeReducer.reduce(state, event, time, autoOpenEnabled)

    private fun withLockStatus(state: WorkingModeState, status: String) =
        reduce(state, WorkingModeEvent.LockStatusReported(status)).state

    @Test
    fun initialAndReadyWithoutStatusAreUnknownAndUnselected() {
        val initialUi = WorkingModeUi.compute(WorkingModeState())
        assertFalse(initialUi.autoSelected)
        assertFalse(initialUi.alwaysOpenSelected)

        val ready = reduce(WorkingModeState(), WorkingModeEvent.BleStateSynced(BleRepository.State.READY)).state
        val readyUi = WorkingModeUi.compute(ready)
        assertEquals(ConfirmedLockMode.UNKNOWN, ready.confirmedLockMode)
        assertFalse(readyUi.autoSelected)
        assertFalse(readyUi.alwaysOpenSelected)
    }

    @Test
    fun requestedOrCachedModeDoesNotBecomeConfirmedWithoutLockStatus() {
        val state = readyUnknown.copy(confirmedAutomaticServiceActive = true)
        assertFalse(WorkingModeUi.compute(state).autoSelected)
        val result = reduce(state, WorkingModeEvent.UserToggledAlwaysOpen)
        assertNull(result.state.transition)
        assertTrue(result.effects.contains(WorkingModeEffect.RequestStatusRefresh))
        assertFalse(WorkingModeUi.compute(result.state).alwaysOpenSelected)
    }

    @Test
    fun autoToAlwaysOpenKeepsConfirmedAutoUntilOpenHold() {
        val initial = readyUnknown.copy(
            confirmedLockMode = ConfirmedLockMode.AUTO,
            confirmedAutomaticServiceActive = true
        )
        val request = reduce(initial, WorkingModeEvent.UserToggledAlwaysOpen, 10L)
        val requestedUi = WorkingModeUi.compute(request.state)
        assertTrue(request.state.transition is WorkingModeTransition.ToAlwaysOpen)
        assertTrue(request.effects.contains(WorkingModeEffect.SendSetWorkingMode(true)))
        assertTrue(requestedUi.autoSelected)
        assertFalse(requestedUi.alwaysOpenSelected)

        // A successful GATT write is not a state event.
        assertTrue(WorkingModeUi.compute(request.state).autoSelected)
        assertFalse(WorkingModeUi.compute(request.state).alwaysOpenSelected)

        val staleStatus = reduce(request.state, WorkingModeEvent.LockStatusReported("LOCKED"), 11L)
        assertTrue(staleStatus.state.transition is WorkingModeTransition.ToAlwaysOpen)
        assertEquals(ConfirmedLockMode.AUTO, staleStatus.state.confirmedLockMode)

        val confirmed = reduce(staleStatus.state, WorkingModeEvent.LockStatusReported("OPEN_HOLD"), 12L)
        val buttons = WorkingModeUi.compute(confirmed.state)
        assertNull(confirmed.state.transition)
        assertEquals(ConfirmedLockMode.ALWAYS_OPEN, confirmed.state.confirmedLockMode)
        assertTrue(buttons.alwaysOpenSelected)
        assertFalse(buttons.autoSelected)
    }

    @Test
    fun alwaysOpenToAutoWaitsForLockThenAutomaticServiceConfirmation() {
        val initial = readyUnknown.copy(
            confirmedLockMode = ConfirmedLockMode.ALWAYS_OPEN,
            confirmedAutomaticServiceActive = false
        )
        val request = reduce(initial, WorkingModeEvent.UserToggledAuto, 20L)
        assertEquals(ConfirmedLockMode.ALWAYS_OPEN, request.state.confirmedLockMode)
        assertTrue(WorkingModeUi.compute(request.state).alwaysOpenSelected)
        assertFalse(WorkingModeUi.compute(request.state).autoSelected)

        val lockConfirmed = reduce(request.state, WorkingModeEvent.LockStatusReported("LOCKED"), 21L)
        assertEquals(ConfirmedLockMode.AUTO, lockConfirmed.state.confirmedLockMode)
        assertTrue(lockConfirmed.state.transition is WorkingModeTransition.ToAuto)
        assertTrue(lockConfirmed.effects.contains(WorkingModeEffect.ResumeAutomaticMode))
        assertFalse(WorkingModeUi.compute(lockConfirmed.state).autoSelected)

        val serviceConfirmed = reduce(
            lockConfirmed.state,
            WorkingModeEvent.AutomaticModeConfirmed(true),
            22L
        )
        assertNull(serviceConfirmed.state.transition)
        assertTrue(WorkingModeUi.compute(serviceConfirmed.state).autoSelected)
        assertFalse(WorkingModeUi.compute(serviceConfirmed.state).alwaysOpenSelected)
    }

    @Test
    fun leavingAlwaysOpenWithEitherModeButtonRequestsImmediateClose() {
        val initial = readyUnknown.copy(
            confirmedLockMode = ConfirmedLockMode.ALWAYS_OPEN,
            confirmedAutomaticServiceActive = false
        )

        val byAlwaysOpen = reduce(initial, WorkingModeEvent.UserToggledAlwaysOpen, 25L)
        val byAuto = reduce(initial, WorkingModeEvent.UserToggledAuto, 26L)
        val disableEffect = byAlwaysOpen.effects
            .filterIsInstance<WorkingModeEffect.SendSetWorkingMode>()
            .single()
        assertFalse(disableEffect.alwaysOpen)
        assertTrue(disableEffect.closeWhenDisablingAlwaysOpen)
        assertTrue(byAlwaysOpen.effects.contains(WorkingModeEffect.PersistAutoOpen(false)))
        assertTrue(byAlwaysOpen.effects.contains(WorkingModeEffect.SendSetAutomaticMode(false)))
        assertFalse((byAlwaysOpen.state.transition as WorkingModeTransition.ToAuto).activateAutomaticService)

        val enableEffect = byAuto.effects
            .filterIsInstance<WorkingModeEffect.SendSetWorkingMode>()
            .single()
        assertFalse(enableEffect.alwaysOpen)
        assertTrue(enableEffect.closeWhenDisablingAlwaysOpen)
        assertTrue((byAuto.state.transition as WorkingModeTransition.ToAuto).activateAutomaticService)
    }

    @Test
    fun automaticServiceSnapshotBeforeLockConfirmationCannotCompleteAutoTransition() {
        val request = reduce(
            readyUnknown.copy(confirmedLockMode = ConfirmedLockMode.ALWAYS_OPEN),
            WorkingModeEvent.UserToggledAuto,
            30L
        )
        val serviceFirst = reduce(request.state, WorkingModeEvent.AutomaticModeConfirmed(true), 31L)
        assertTrue(serviceFirst.state.transition is WorkingModeTransition.ToAuto)
        assertFalse(WorkingModeUi.compute(serviceFirst.state).autoSelected)
        val complete = reduce(serviceFirst.state, WorkingModeEvent.LockStatusReported("OPEN"), 32L)
        assertNull(complete.state.transition)
        assertTrue(WorkingModeUi.compute(complete.state).autoSelected)
    }

    @Test
    fun automaticOffIsConfirmedOnlyByInactiveServiceSnapshot() {
        val initial = readyUnknown.copy(
            confirmedLockMode = ConfirmedLockMode.AUTO,
            confirmedAutomaticServiceActive = true
        )
        val requested = reduce(initial, WorkingModeEvent.UserToggledAuto, 40L)
        assertTrue(WorkingModeUi.compute(requested.state).autoSelected)
        assertTrue(requested.state.transition is WorkingModeTransition.AutomaticServiceToInactive)
        assertTrue(requested.effects.contains(WorkingModeEffect.SendSetAutomaticMode(false)))

        val confirmed = reduce(requested.state, WorkingModeEvent.AutomaticModeConfirmed(false), 41L)
        assertNull(confirmed.state.transition)
        assertFalse(WorkingModeUi.compute(confirmed.state).autoSelected)
        assertEquals(ConfirmedLockMode.AUTO, confirmed.state.confirmedLockMode)
    }

    @Test
    fun failedWriteKeepsPreviousConfirmedModeAndClearsRequest() {
        val initial = readyUnknown.copy(
            confirmedLockMode = ConfirmedLockMode.AUTO,
            confirmedAutomaticServiceActive = true
        )
        val requested = reduce(initial, WorkingModeEvent.UserToggledAlwaysOpen, 50L)
        val failed = reduce(requested.state, WorkingModeEvent.WorkingModeCommandFailed, 51L)

        assertNull(failed.state.transition)
        assertEquals(ConfirmedLockMode.AUTO, failed.state.confirmedLockMode)
        assertTrue(WorkingModeUi.compute(failed.state).autoSelected)
        assertFalse(WorkingModeUi.compute(failed.state).alwaysOpenSelected)
    }

    @Test
    fun timeoutClearsPendingWithoutChangingConfirmedModeAndRequestsRefresh() {
        val initial = readyUnknown.copy(
            confirmedLockMode = ConfirmedLockMode.AUTO,
            confirmedAutomaticServiceActive = true
        )
        val requested = reduce(initial, WorkingModeEvent.UserToggledAlwaysOpen, 60L)
        val timedOut = reduce(
            requested.state,
            WorkingModeEvent.TransitionTimedOut(60L),
            12_060L
        )

        assertNull(timedOut.state.transition)
        assertEquals(ConfirmedLockMode.AUTO, timedOut.state.confirmedLockMode)
        assertTrue(WorkingModeUi.compute(timedOut.state).autoSelected)
        assertFalse(WorkingModeUi.compute(timedOut.state).alwaysOpenSelected)
        assertTrue(timedOut.effects.contains(WorkingModeEffect.RequestStatusRefresh))
    }

    @Test
    fun disconnectDuringTransitionClearsPendingAndMakesLockUnknown() {
        val requested = reduce(
            readyUnknown.copy(confirmedLockMode = ConfirmedLockMode.AUTO),
            WorkingModeEvent.UserToggledAlwaysOpen,
            70L
        )
        val lost = reduce(requested.state, WorkingModeEvent.ConnectionLost, 71L)

        assertNull(lost.state.transition)
        assertEquals(ConfirmedLockMode.UNKNOWN, lost.state.confirmedLockMode)
        assertFalse(WorkingModeUi.compute(lost.state).alwaysOpenSelected)
        assertTrue(lost.effects.contains(WorkingModeEffect.CancelTimeout))
    }

    @Test
    fun toAlwaysOpenIgnoresStaleAutoStatusAndToAutoIgnoresOpenHold() {
        val toAlways = reduce(
            readyUnknown.copy(confirmedLockMode = ConfirmedLockMode.AUTO),
            WorkingModeEvent.UserToggledAlwaysOpen,
            80L
        )
        val autoStatus = reduce(toAlways.state, WorkingModeEvent.LockStatusReported("OPEN"), 81L)
        assertTrue(autoStatus.state.transition is WorkingModeTransition.ToAlwaysOpen)

        val toAuto = reduce(
            readyUnknown.copy(confirmedLockMode = ConfirmedLockMode.ALWAYS_OPEN),
            WorkingModeEvent.UserToggledAuto,
            82L
        )
        val holdStatus = reduce(toAuto.state, WorkingModeEvent.LockStatusReported("OPEN_HOLD"), 83L)
        assertTrue(holdStatus.state.transition is WorkingModeTransition.ToAuto)
        assertFalse(holdStatus.effects.contains(WorkingModeEffect.ResumeAutomaticMode))
    }

    @Test
    fun duplicateStatusAndServiceEventsAreIdempotent() {
        val firstStatus = withLockStatus(readyUnknown, "LOCKED")
        assertEquals(firstStatus, withLockStatus(firstStatus, "LOCKED"))
        val firstService = reduce(firstStatus, WorkingModeEvent.AutomaticModeConfirmed(true)).state
        assertEquals(
            firstService,
            reduce(firstService, WorkingModeEvent.AutomaticModeConfirmed(true)).state
        )
    }

    @Test
    fun lateTimeoutAfterConfirmationDoesNotChangeState() {
        val request = reduce(
            readyUnknown.copy(confirmedLockMode = ConfirmedLockMode.AUTO),
            WorkingModeEvent.UserToggledAlwaysOpen,
            90L
        )
        val confirmed = reduce(request.state, WorkingModeEvent.LockStatusReported("OPEN_HOLD"), 91L)
        val lateTimeout = reduce(confirmed.state, WorkingModeEvent.TransitionTimedOut(90L), 20_000L)

        assertEquals(confirmed.state, lateTimeout.state)
        assertTrue(lateTimeout.effects.isEmpty())
    }

    @Test
    fun repeatedClickWhilePendingDoesNotSendAnotherCommand() {
        val first = reduce(
            readyUnknown.copy(confirmedLockMode = ConfirmedLockMode.AUTO),
            WorkingModeEvent.UserToggledAlwaysOpen,
            100L
        )
        val second = reduce(first.state, WorkingModeEvent.UserToggledAlwaysOpen, 101L)
        assertEquals(first.state, second.state)
        assertTrue(second.effects.isEmpty())
    }
}
