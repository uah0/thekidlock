package com.blelock.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class WorkingModeReducerTest {
    private val ready = WorkingModeUiState(bleState = BleRepository.State.READY)

    @Test
    fun initialStateDisablesBothButtonsWithoutHighlights() {
        val buttons = WorkingModeUi.compute(WorkingModeUiState())

        assertEquals(false, buttons.autoEnabled)
        assertEquals(false, buttons.keepOpenEnabled)
        assertEquals(false, buttons.autoHighlighted)
        assertEquals(false, buttons.keepOpenHighlighted)
    }

    @Test
    fun readyEnablesBothButtons() {
        val result = WorkingModeReducer.reduce(
            WorkingModeUiState(),
            WorkingModeEvent.BleStateSynced(BleRepository.State.READY),
            1L
        )
        val buttons = WorkingModeUi.compute(result.state)

        assertEquals(true, buttons.autoEnabled)
        assertEquals(true, buttons.keepOpenEnabled)
    }

    @Test
    fun lockedStatusReportsAutoModeWithoutEffects() {
        val result = WorkingModeReducer.reduce(
            ready,
            WorkingModeEvent.LockStatusReported("LOCKED"),
            1L
        )

        assertEquals(KnownWorkingMode.AUTO, result.state.knownMode)
        assertTrue(result.effects.isEmpty())
    }

    @Test
    fun automaticModeBroadcastHighlightsAutoOnly() {
        val withStatus = WorkingModeReducer.reduce(
            ready,
            WorkingModeEvent.LockStatusReported("LOCKED"),
            1L
        ).state
        val result = WorkingModeReducer.reduce(
            withStatus,
            WorkingModeEvent.AutomaticModeBroadcastReceived(true),
            2L
        )
        val buttons = WorkingModeUi.compute(result.state)

        assertEquals(true, buttons.autoHighlighted)
        assertEquals(false, buttons.keepOpenHighlighted)
    }

    @Test
    fun autoToggleIsIgnoredWhenBleIsNotReady() {
        val initial = WorkingModeUiState()
        val result = WorkingModeReducer.reduce(initial, WorkingModeEvent.UserToggledAuto, 1L)

        assertEquals(initial, result.state)
        assertTrue(result.effects.isEmpty())
    }

    @Test
    fun autoToggleIsIgnoredWhileAnotherCommandIsPending() {
        val initial = ready.copy(
            pending = WorkingModePendingCommand.SwitchingToAlwaysOpen(1L)
        )
        val result = WorkingModeReducer.reduce(initial, WorkingModeEvent.UserToggledAuto, 2L)

        assertEquals(initial, result.state)
        assertTrue(result.effects.isEmpty())
    }

    @Test
    fun autoFromAlwaysOpenSchedulesModeChangeAndLocksButtons() {
        val initial = ready.copy(knownMode = KnownWorkingMode.ALWAYS_OPEN)
        val result = WorkingModeReducer.reduce(initial, WorkingModeEvent.UserToggledAuto, 7L)
        val buttons = WorkingModeUi.compute(result.state)

        assertTrue(result.state.pending is WorkingModePendingCommand.SwitchingToAuto)
        assertEquals(
            true,
            (result.state.pending as WorkingModePendingCommand.SwitchingToAuto).resumeAfterConfirm
        )
        assertTrue(result.effects.contains(
            WorkingModeEffect.SendSetWorkingMode(alwaysOpen = false, persistAutoOpen = true)
        ))
        assertTrue(result.effects.contains(WorkingModeEffect.ScheduleTimeout(7L + WorkingModeReducer.COMMAND_TIMEOUT_MS)))
        assertEquals(false, buttons.autoEnabled)
        assertEquals(false, buttons.autoHighlighted)
        assertEquals(false, buttons.keepOpenHighlighted)
    }

    @Test
    fun confirmedAutoAfterAlwaysOpenCancelsTimeoutAndResumesAuto() {
        val pending = WorkingModeReducer.reduce(
            ready.copy(knownMode = KnownWorkingMode.ALWAYS_OPEN),
            WorkingModeEvent.UserToggledAuto,
            7L
        )
        val result = WorkingModeReducer.reduce(
            pending.state,
            WorkingModeEvent.LockStatusReported("LOCKED"),
            8L
        )
        val buttons = WorkingModeUi.compute(result.state)

        assertEquals(WorkingModePendingCommand.None, result.state.pending)
        assertTrue(result.effects.contains(WorkingModeEffect.CancelTimeout))
        assertTrue(result.effects.contains(WorkingModeEffect.ResumeAutomaticMode))
        assertEquals(true, buttons.autoEnabled)
    }

    @Test
    fun autoToggleWhenAlreadyAutoStartsTurningAutoOff() {
        val initial = ready.copy(
            knownMode = KnownWorkingMode.AUTO,
            automaticModeActive = true
        )
        val result = WorkingModeReducer.reduce(initial, WorkingModeEvent.UserToggledAuto, 3L)

        assertTrue(result.state.pending is WorkingModePendingCommand.TurningAutoOff)
        assertTrue(result.effects.contains(WorkingModeEffect.SendSetAutomaticMode(false)))
    }

    @Test
    fun keepOpenToggleFromAutoSendsAlwaysOpen() {
        val initial = ready.copy(knownMode = KnownWorkingMode.AUTO)
        val result = WorkingModeReducer.reduce(initial, WorkingModeEvent.UserToggledKeepOpen, 4L)

        assertTrue(result.state.pending is WorkingModePendingCommand.SwitchingToAlwaysOpen)
        assertTrue(result.effects.contains(WorkingModeEffect.SendSetWorkingMode(true)))
    }

    @Test
    fun leavingAlwaysOpenResumesPersistedAutomaticModeAfterConfirmation() {
        val initial = ready.copy(knownMode = KnownWorkingMode.ALWAYS_OPEN)
        val result = WorkingModeReducer.reduce(
            initial, WorkingModeEvent.UserToggledKeepOpen, 4L, autoOpenEnabled = true
        )
        val pending = result.state.pending as WorkingModePendingCommand.SwitchingToAlwaysOpen

        assertEquals(true, pending.resumeAfterConfirm)
        assertTrue(result.effects.contains(
            WorkingModeEffect.SendSetWorkingMode(alwaysOpen = false, persistAutoOpen = true)
        ))
        val confirmed = WorkingModeReducer.reduce(
            result.state, WorkingModeEvent.LockStatusReported("LOCKED"), 5L, true
        )
        assertTrue(confirmed.effects.contains(WorkingModeEffect.ResumeAutomaticMode))
    }

    @Test
    fun openHoldConfirmationHighlightsKeepOpenOnly() {
        val pending = WorkingModeReducer.reduce(
            ready.copy(knownMode = KnownWorkingMode.AUTO),
            WorkingModeEvent.UserToggledKeepOpen,
            4L
        )
        val activeWhilePending = WorkingModeReducer.reduce(
            pending.state,
            WorkingModeEvent.AutomaticModeBroadcastReceived(true),
            5L
        )
        val result = WorkingModeReducer.reduce(
            activeWhilePending.state,
            WorkingModeEvent.LockStatusReported("OPEN_HOLD"),
            6L
        )
        val buttons = WorkingModeUi.compute(result.state)

        assertEquals(KnownWorkingMode.ALWAYS_OPEN, result.state.knownMode)
        assertEquals(true, buttons.keepOpenHighlighted)
        assertEquals(false, buttons.autoHighlighted)
    }

    @Test
    fun connectionLossClearsAutoSwitchPendingCommand() {
        val initial = ready.copy(
            pending = WorkingModePendingCommand.SwitchingToAuto(1L, resumeAfterConfirm = false)
        )
        val result = WorkingModeReducer.reduce(initial, WorkingModeEvent.ConnectionLost, 2L)

        assertEquals(WorkingModePendingCommand.None, result.state.pending)
        assertTrue(result.effects.contains(WorkingModeEffect.CancelTimeout))
    }

    @Test
    fun errorStateClearsAlwaysOpenPendingCommand() {
        val initial = ready.copy(
            pending = WorkingModePendingCommand.SwitchingToAlwaysOpen(1L)
        )
        val result = WorkingModeReducer.reduce(
            initial,
            WorkingModeEvent.BleStateSynced(BleRepository.State.ERROR),
            2L
        )

        assertEquals(WorkingModePendingCommand.None, result.state.pending)
        assertTrue(result.effects.contains(WorkingModeEffect.CancelTimeout))
    }

    @Test
    fun pendingCommandTimeoutClearsPending() {
        val initial = ready.copy(
            knownMode = KnownWorkingMode.AUTO,
            automaticModeActive = true,
            pending = WorkingModePendingCommand.TurningAutoOff(
                1L, previousAutomaticModeActive = true, previousKnownMode = KnownWorkingMode.AUTO
            )
        )
        val result = WorkingModeReducer.reduce(initial, WorkingModeEvent.PendingCommandTimedOut, 2L)

        assertEquals(WorkingModePendingCommand.None, result.state.pending)
        assertEquals(true, result.state.automaticModeActive)
        assertEquals(KnownWorkingMode.AUTO, result.state.knownMode)
        assertTrue(result.effects.contains(WorkingModeEffect.RequestStatusRefresh))
    }

    @Test
    fun timedOutModeChangeRestoresPriorModeSnapshot() {
        val initial = ready.copy(
            knownMode = KnownWorkingMode.ALWAYS_OPEN,
            automaticModeActive = true,
            pending = WorkingModePendingCommand.SwitchingToAuto(
                10L, true, previousAutomaticModeActive = true,
                previousKnownMode = KnownWorkingMode.ALWAYS_OPEN
            )
        )
        val timedOut = WorkingModeReducer.reduce(initial, WorkingModeEvent.PendingCommandTimedOut, 20L)

        assertEquals(KnownWorkingMode.ALWAYS_OPEN, timedOut.state.knownMode)
        assertEquals(true, timedOut.state.automaticModeActive)
        assertEquals(false, WorkingModeUi.compute(timedOut.state).autoHighlighted)
        assertEquals(true, WorkingModeUi.compute(timedOut.state).keepOpenHighlighted)
    }

    @Test
    fun timeoutWithoutPendingCommandIsNoOp() {
        val result = WorkingModeReducer.reduce(
            ready,
            WorkingModeEvent.PendingCommandTimedOut,
            2L
        )

        assertEquals(ready, result.state)
        assertTrue(result.effects.isEmpty())
    }

    @Test
    fun syncingReadyWhenAlreadyReadyIsIdempotent() {
        val result = WorkingModeReducer.reduce(
            ready,
            WorkingModeEvent.BleStateSynced(BleRepository.State.READY),
            2L
        )

        assertEquals(ready, result.state)
        assertTrue(result.effects.isEmpty())
    }

    @Test
    fun repeatedAutoToggleWhilePendingSendsOnlyOneCommand() {
        val first = WorkingModeReducer.reduce(
            ready.copy(knownMode = KnownWorkingMode.AUTO, automaticModeActive = true),
            WorkingModeEvent.UserToggledAuto,
            1L
        )
        val second = WorkingModeReducer.reduce(
            first.state,
            WorkingModeEvent.UserToggledAuto,
            2L
        )
        val sentCommands = (first.effects + second.effects)
            .count { it is WorkingModeEffect.SendSetAutomaticMode }

        assertEquals(first.state, second.state)
        assertEquals(1, sentCommands)
    }

    @Test
    fun unrecognizedLockStatusDoesNotChangeModeOrPending() {
        val result = WorkingModeReducer.reduce(
            ready,
            WorkingModeEvent.LockStatusReported("ERROR_WIFI"),
            2L
        )

        assertEquals(ready.knownMode, result.state.knownMode)
        assertEquals(ready.pending, result.state.pending)
    }

    @Test
    fun autoBroadcastDoesNotUnlockButtonsWhileCommandPending() {
        val initial = ready.copy(
            pending = WorkingModePendingCommand.SwitchingToAlwaysOpen(1L)
        )
        val result = WorkingModeReducer.reduce(
            initial,
            WorkingModeEvent.AutomaticModeBroadcastReceived(true),
            2L
        )
        val buttons = WorkingModeUi.compute(result.state)

        assertEquals(true, result.state.automaticModeActive)
        assertEquals(false, buttons.keepOpenEnabled)
    }

    @Test
    fun firstReadySyncEnablesButtonsForPreviouslyReadyService() {
        val activityState = WorkingModeReducer.reduce(
            WorkingModeUiState(),
            WorkingModeEvent.BleStateSynced(BleRepository.State.READY),
            1L
        ).state
        val buttons = WorkingModeUi.compute(activityState)

        assertEquals(true, buttons.autoEnabled)
        assertEquals(true, buttons.keepOpenEnabled)
    }
}
