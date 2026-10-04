package com.blelock.app

data class WorkingModeState(
    val autoOpenEnabled: Boolean = false,
    val alwaysOpenEnabled: Boolean = false
)

data class WorkingModeButtonsUi(
    val autoSelected: Boolean,
    val alwaysOpenSelected: Boolean,
    val autoEnabled: Boolean,
    val alwaysOpenEnabled: Boolean
)

sealed class WorkingModeEvent {
    data class AutoOpenChanged(val enabled: Boolean) : WorkingModeEvent()
    data class AlwaysOpenConfirmed(val enabled: Boolean) : WorkingModeEvent()
}

object WorkingModeReducer {
    fun reduce(state: WorkingModeState, event: WorkingModeEvent): WorkingModeState = when (event) {
        is WorkingModeEvent.AutoOpenChanged -> state.copy(autoOpenEnabled = event.enabled)
        is WorkingModeEvent.AlwaysOpenConfirmed -> state.copy(alwaysOpenEnabled = event.enabled)
    }
}

object WorkingModeUi {
    fun compute(state: WorkingModeState, alwaysOpenAvailable: Boolean): WorkingModeButtonsUi =
        WorkingModeButtonsUi(
            autoSelected = state.autoOpenEnabled,
            alwaysOpenSelected = state.alwaysOpenEnabled,
            autoEnabled = true,
            alwaysOpenEnabled = alwaysOpenAvailable
        )
}
