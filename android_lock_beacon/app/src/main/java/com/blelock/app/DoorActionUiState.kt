package com.blelock.app

enum class ConfirmedLockState { LOCKED, UNLOCKED }

sealed interface DoorActionUiState {
    data class Idle(
        val lastConfirmedLock: ConfirmedLockState = ConfirmedLockState.LOCKED,
        val doorOpen: Boolean = false
    ) : DoorActionUiState
    data class Sending(val brokerAccepted: Boolean = false) : DoorActionUiState
    data object Unlocked : DoorActionUiState
    data object DoorOpen : DoorActionUiState
    data object Completed : DoorActionUiState
    data class Error(
        val lastConfirmedLock: ConfirmedLockState,
        val doorOpen: Boolean = false,
        val operationFinished: Boolean = false,
        val message: String
    ) : DoorActionUiState
}

sealed interface DoorActionEvent {
    data object Started : DoorActionEvent
    data object BrokerAccepted : DoorActionEvent
    data class LockUnlocked(val healthy: Boolean = true) : DoorActionEvent
    data object DoorOpened : DoorActionEvent
    data object Closing : DoorActionEvent
    data class LockClosed(val healthy: Boolean = true) : DoorActionEvent
    data object OperationFinished : DoorActionEvent
    data object CompletedAnimationFinished : DoorActionEvent
    data class Failed(val message: String) : DoorActionEvent
}

object DoorActionReducer {
    fun reduce(state: DoorActionUiState, event: DoorActionEvent): DoorActionUiState = when (event) {
        DoorActionEvent.Started -> if (state is DoorActionUiState.Idle ||
            state is DoorActionUiState.Completed || state is DoorActionUiState.Error) {
            DoorActionUiState.Sending()
        } else state

        DoorActionEvent.BrokerAccepted -> if (state is DoorActionUiState.Sending)
            state.copy(brokerAccepted = true) else state

        is DoorActionEvent.LockUnlocked -> if (state is DoorActionUiState.Sending ||
            state is DoorActionUiState.Idle) {
            if (event.healthy) DoorActionUiState.Unlocked else
                DoorActionUiState.Error(ConfirmedLockState.UNLOCKED, operationFinished = false, message = "Ошибка замка")
        } else state

        // No current MQTT/BLE event confirms physical door movement. This event is
        // reserved for a future real sensor notification and is not synthesized.
        DoorActionEvent.DoorOpened -> if (state is DoorActionUiState.Unlocked)
            DoorActionUiState.DoorOpen else state

        DoorActionEvent.Closing -> state

        is DoorActionEvent.LockClosed -> if (state is DoorActionUiState.Unlocked ||
            state is DoorActionUiState.DoorOpen || state is DoorActionUiState.Sending) {
            if (event.healthy) DoorActionUiState.Completed else
                DoorActionUiState.Error(ConfirmedLockState.LOCKED, operationFinished = true, message = "Ошибка замка")
        } else state

        DoorActionEvent.OperationFinished -> if (state is DoorActionUiState.Error)
            state.copy(operationFinished = true) else state

        DoorActionEvent.CompletedAnimationFinished -> if (state is DoorActionUiState.Completed)
            DoorActionUiState.Idle() else if (state is DoorActionUiState.Error)
                DoorActionUiState.Idle(state.lastConfirmedLock, state.doorOpen) else state

        is DoorActionEvent.Failed -> DoorActionUiState.Error(
            lastConfirmedLock = when (state) {
                is DoorActionUiState.Unlocked, is DoorActionUiState.DoorOpen -> ConfirmedLockState.UNLOCKED
                is DoorActionUiState.Idle -> state.lastConfirmedLock
                is DoorActionUiState.Error -> state.lastConfirmedLock
                else -> ConfirmedLockState.LOCKED
            },
            doorOpen = when (state) {
                is DoorActionUiState.DoorOpen -> true
                is DoorActionUiState.Idle -> state.doorOpen
                is DoorActionUiState.Error -> state.doorOpen
                else -> false
            },
            operationFinished = true,
            message = event.message
        )
    }

    fun accessibilityDescription(state: DoorActionUiState): String = when (state) {
        is DoorActionUiState.Idle -> when {
            state.doorOpen -> "Дверь открыта"
            state.lastConfirmedLock == ConfirmedLockState.UNLOCKED -> "Замок открыт"
            else -> "Дверь закрыта и замок закрыт"
        }
        is DoorActionUiState.Sending -> if (state.brokerAccepted)
            "Команда передана, ожидание замка" else "Команда отправляется"
        DoorActionUiState.Unlocked -> "Замок открыт"
        DoorActionUiState.DoorOpen -> "Дверь открыта"
        DoorActionUiState.Completed -> "Дверь закрыта и заперта"
        is DoorActionUiState.Error -> if (state.doorOpen)
            "Ошибка. Последнее подтверждение: дверь открыта" else if (state.lastConfirmedLock == ConfirmedLockState.UNLOCKED)
            "Ошибка. Последнее подтверждение: замок открыт" else
            "Ошибка. Последнее подтверждение: дверь закрыта и замок закрыт"
    }
}
