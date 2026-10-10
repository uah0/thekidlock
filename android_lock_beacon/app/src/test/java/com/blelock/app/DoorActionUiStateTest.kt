package com.blelock.app

import org.junit.Assert.assertEquals
import org.junit.Test

class DoorActionUiStateTest {
    @Test fun operationProgressUsesDeviceEventsAndCompletesOnlyWhenLocked() {
        var state: DoorActionUiState = DoorActionUiState.Idle()
        state = DoorActionReducer.reduce(state, DoorActionEvent.Started)
        assertEquals(DoorActionUiState.Sending(), state)
        state = DoorActionReducer.reduce(state, DoorActionEvent.BrokerAccepted)
        assertEquals(DoorActionUiState.Sending(brokerAccepted = true), state)
        assertEquals(state, DoorActionReducer.reduce(state, DoorActionEvent.BrokerAccepted))
        state = DoorActionReducer.reduce(state, DoorActionEvent.LockUnlocked())
        assertEquals(DoorActionUiState.Unlocked, state)
        assertEquals(state, DoorActionReducer.reduce(state, DoorActionEvent.Closing))
        state = DoorActionReducer.reduce(state, DoorActionEvent.LockClosed())
        assertEquals(DoorActionUiState.Completed, state)
        state = DoorActionReducer.reduce(state, DoorActionEvent.CompletedAnimationFinished)
        assertEquals(DoorActionUiState.Idle(), state)
    }

    @Test fun failureNeverReportsSuccessAndKeepsLastConfirmedPhysicalState() {
        val beforeUnlock = DoorActionReducer.reduce(
            DoorActionUiState.Sending(), DoorActionEvent.Failed("timeout")
        ) as DoorActionUiState.Error
        assertEquals(ConfirmedLockState.LOCKED, beforeUnlock.lastConfirmedLock)

        val afterUnlock = DoorActionReducer.reduce(
            DoorActionUiState.Unlocked, DoorActionEvent.Failed("connection lost")
        ) as DoorActionUiState.Error
        assertEquals(ConfirmedLockState.UNLOCKED, afterUnlock.lastConfirmedLock)
        assertEquals(afterUnlock, DoorActionReducer.reduce(afterUnlock, DoorActionEvent.LockClosed()))
        assertEquals(DoorActionUiState.Idle(ConfirmedLockState.UNLOCKED),
            DoorActionReducer.reduce(afterUnlock, DoorActionEvent.CompletedAnimationFinished))
    }

    @Test fun faultyPhysicalConfirmationsNeverShowCompletion() {
        val unlockError = DoorActionReducer.reduce(
            DoorActionUiState.Sending(), DoorActionEvent.LockUnlocked(healthy = false)
        ) as DoorActionUiState.Error
        assertEquals(ConfirmedLockState.UNLOCKED, unlockError.lastConfirmedLock)
        assertEquals(false, unlockError.operationFinished)

        val closeError = DoorActionReducer.reduce(
            DoorActionUiState.Unlocked, DoorActionEvent.LockClosed(healthy = false)
        ) as DoorActionUiState.Error
        assertEquals(ConfirmedLockState.LOCKED, closeError.lastConfirmedLock)
        assertEquals(true, closeError.operationFinished)
    }

    @Test fun doorOpenRequiresAnExplicitPhysicalEvent() {
        assertEquals(DoorActionUiState.Sending(), DoorActionReducer.reduce(
            DoorActionUiState.Sending(), DoorActionEvent.DoorOpened
        ))
        assertEquals(DoorActionUiState.DoorOpen, DoorActionReducer.reduce(
            DoorActionUiState.Unlocked, DoorActionEvent.DoorOpened
        ))
    }

    @Test fun accessibilityDescriptionFollowsState() {
        assertEquals(R.string.lock_open, DoorActionReducer.accessibilityDescriptionResource(DoorActionUiState.Unlocked))
        assertEquals(R.string.internet_closed_locked, DoorActionReducer.accessibilityDescriptionResource(DoorActionUiState.Completed))
    }
}
