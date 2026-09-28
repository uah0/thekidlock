package com.blelock.app

enum class KnownWorkingMode {
    AUTO,
    ALWAYS_OPEN
}

sealed class WorkingModePendingCommand {
    object None : WorkingModePendingCommand()
    data class TurningAutoOff(
        val requestedAtMs: Long,
        val previousAutomaticModeActive: Boolean = false,
        val previousKnownMode: KnownWorkingMode? = null
    ) : WorkingModePendingCommand()
    data class SwitchingToAuto(
        val requestedAtMs: Long,
        val resumeAfterConfirm: Boolean,
        val previousAutomaticModeActive: Boolean = false,
        val previousKnownMode: KnownWorkingMode? = null
    ) : WorkingModePendingCommand()
    data class SwitchingToAlwaysOpen(
        val requestedAtMs: Long,
        val resumeAfterConfirm: Boolean = false,
        val previousAutomaticModeActive: Boolean = false,
        val previousKnownMode: KnownWorkingMode? = null
    ) : WorkingModePendingCommand()
}

data class WorkingModeUiState(
    val bleState: BleRepository.State = BleRepository.State.IDLE,
    val pending: WorkingModePendingCommand = WorkingModePendingCommand.None,
    val knownMode: KnownWorkingMode? = null,
    val automaticModeActive: Boolean = BleLockService.lastAutomaticMode
)

data class WorkingModeButtons(
    val autoEnabled: Boolean,
    val autoHighlighted: Boolean,
    val keepOpenEnabled: Boolean,
    val keepOpenHighlighted: Boolean
)

object WorkingModeUi {
    fun compute(state: WorkingModeUiState): WorkingModeButtons {
        val enabled = state.bleState == BleRepository.State.READY &&
            state.pending == WorkingModePendingCommand.None
        return WorkingModeButtons(
            autoEnabled = enabled,
            autoHighlighted = state.pending == WorkingModePendingCommand.None &&
                state.automaticModeActive && state.knownMode == KnownWorkingMode.AUTO,
            keepOpenEnabled = enabled,
            keepOpenHighlighted = state.pending == WorkingModePendingCommand.None &&
                state.knownMode == KnownWorkingMode.ALWAYS_OPEN
        )
    }
}

sealed class WorkingModeEvent {
    data class BleStateSynced(val state: BleRepository.State) : WorkingModeEvent()
    object ConnectionLost : WorkingModeEvent()
    data class LockStatusReported(val status: String) : WorkingModeEvent()
    data class AutomaticModeBroadcastReceived(val active: Boolean) : WorkingModeEvent()
    object UserToggledAuto : WorkingModeEvent()
    object UserToggledKeepOpen : WorkingModeEvent()
    object PendingCommandTimedOut : WorkingModeEvent()
}

sealed class WorkingModeEffect {
    data class SendSetAutomaticMode(val enabled: Boolean) : WorkingModeEffect()
    data class SendSetWorkingMode(
        val alwaysOpen: Boolean,
        val persistAutoOpen: Boolean = false
    ) : WorkingModeEffect()
    object ResumeAutomaticMode : WorkingModeEffect()
    data class ScheduleTimeout(val atMs: Long) : WorkingModeEffect()
    object CancelTimeout : WorkingModeEffect()
    object RequestStatusRefresh : WorkingModeEffect()
}

data class ReduceResult(
    val state: WorkingModeUiState,
    val effects: List<WorkingModeEffect> = emptyList()
)

object WorkingModeReducer {
    const val COMMAND_TIMEOUT_MS = 12_000L

    fun reduce(
        state: WorkingModeUiState,
        event: WorkingModeEvent,
        nowMs: Long,
        autoOpenEnabled: Boolean = false
    ): ReduceResult {
        return when (event) {
            is WorkingModeEvent.BleStateSynced -> {
                val next = state.copy(bleState = event.state)
                if (event.state == BleRepository.State.ERROR) {
                    ReduceResult(
                        next.copy(pending = WorkingModePendingCommand.None),
                        listOf(WorkingModeEffect.CancelTimeout)
                    )
                } else {
                    ReduceResult(next)
                }
            }

            WorkingModeEvent.ConnectionLost -> ReduceResult(
                state.copy(pending = WorkingModePendingCommand.None),
                listOf(WorkingModeEffect.CancelTimeout)
            )

            is WorkingModeEvent.LockStatusReported -> {
                val known = when (event.status.trim().uppercase()) {
                    "OPEN", "LOCKED" -> KnownWorkingMode.AUTO
                    "OPEN_HOLD" -> KnownWorkingMode.ALWAYS_OPEN
                    else -> return ReduceResult(state)
                }
                var next = state.copy(knownMode = known)
                val effects = mutableListOf<WorkingModeEffect>()
                val pending = state.pending
                if (pending != WorkingModePendingCommand.None) {
                    next = next.copy(pending = WorkingModePendingCommand.None)
                    effects += WorkingModeEffect.CancelTimeout
                    val shouldResume = when (pending) {
                        is WorkingModePendingCommand.SwitchingToAuto -> pending.resumeAfterConfirm
                        is WorkingModePendingCommand.SwitchingToAlwaysOpen -> pending.resumeAfterConfirm
                        else -> false
                    }
                    if (shouldResume) {
                        effects += WorkingModeEffect.ResumeAutomaticMode
                    }
                }
                ReduceResult(next, effects)
            }

            is WorkingModeEvent.AutomaticModeBroadcastReceived -> ReduceResult(
                state.copy(automaticModeActive = event.active)
            )

            WorkingModeEvent.UserToggledAuto -> {
                if (state.bleState != BleRepository.State.READY ||
                    state.pending != WorkingModePendingCommand.None
                ) {
                    return ReduceResult(state)
                }
                val currentlyAuto = state.automaticModeActive &&
                    state.knownMode == KnownWorkingMode.AUTO
                when {
                    currentlyAuto -> ReduceResult(
                        state.copy(
                            automaticModeActive = false,
                            pending = WorkingModePendingCommand.TurningAutoOff(
                                nowMs, state.automaticModeActive, state.knownMode
                            )
                        ),
                        listOf(
                            WorkingModeEffect.SendSetAutomaticMode(false),
                            WorkingModeEffect.ScheduleTimeout(nowMs + COMMAND_TIMEOUT_MS)
                        )
                    )

                    state.knownMode == KnownWorkingMode.ALWAYS_OPEN -> ReduceResult(
                        state.copy(
                            automaticModeActive = false,
                            pending = WorkingModePendingCommand.SwitchingToAuto(
                                nowMs,
                                resumeAfterConfirm = true,
                                previousAutomaticModeActive = state.automaticModeActive,
                                previousKnownMode = state.knownMode
                            )
                        ),
                        listOf(
                            WorkingModeEffect.SendSetWorkingMode(
                                alwaysOpen = false,
                                persistAutoOpen = true
                            ),
                            WorkingModeEffect.ScheduleTimeout(nowMs + COMMAND_TIMEOUT_MS)
                        )
                    )

                    else -> ReduceResult(
                        state.copy(
                            automaticModeActive = false,
                            pending = WorkingModePendingCommand.SwitchingToAuto(
                                nowMs,
                                resumeAfterConfirm = false,
                                previousAutomaticModeActive = state.automaticModeActive,
                                previousKnownMode = state.knownMode
                            )
                        ),
                        listOf(
                            WorkingModeEffect.SendSetAutomaticMode(true),
                            WorkingModeEffect.ScheduleTimeout(nowMs + COMMAND_TIMEOUT_MS)
                        )
                    )
                }
            }

            WorkingModeEvent.UserToggledKeepOpen -> {
                if (state.bleState != BleRepository.State.READY ||
                    state.pending != WorkingModePendingCommand.None
                ) {
                    return ReduceResult(state)
                }
                val requestedAlwaysOpen = state.knownMode != KnownWorkingMode.ALWAYS_OPEN
                ReduceResult(
                    state.copy(
                        pending = WorkingModePendingCommand.SwitchingToAlwaysOpen(
                            requestedAtMs = nowMs,
                            resumeAfterConfirm = !requestedAlwaysOpen &&
                                state.knownMode == KnownWorkingMode.ALWAYS_OPEN && autoOpenEnabled,
                            previousAutomaticModeActive = state.automaticModeActive,
                            previousKnownMode = state.knownMode
                        )
                    ),
                    listOf(
                        WorkingModeEffect.SendSetWorkingMode(
                            alwaysOpen = requestedAlwaysOpen,
                            persistAutoOpen = !requestedAlwaysOpen && autoOpenEnabled
                        ),
                        WorkingModeEffect.ScheduleTimeout(nowMs + COMMAND_TIMEOUT_MS)
                    )
                )
            }

            WorkingModeEvent.PendingCommandTimedOut -> {
                if (state.pending == WorkingModePendingCommand.None) {
                    ReduceResult(state)
                } else {
                    val pending = state.pending
                    val previousAutomatic = when (pending) {
                        is WorkingModePendingCommand.TurningAutoOff -> pending.previousAutomaticModeActive
                        is WorkingModePendingCommand.SwitchingToAuto -> pending.previousAutomaticModeActive
                        is WorkingModePendingCommand.SwitchingToAlwaysOpen -> pending.previousAutomaticModeActive
                        WorkingModePendingCommand.None -> state.automaticModeActive
                    }
                    val previousKnown = when (pending) {
                        is WorkingModePendingCommand.TurningAutoOff -> pending.previousKnownMode
                        is WorkingModePendingCommand.SwitchingToAuto -> pending.previousKnownMode
                        is WorkingModePendingCommand.SwitchingToAlwaysOpen -> pending.previousKnownMode
                        WorkingModePendingCommand.None -> state.knownMode
                    }
                    ReduceResult(
                        state.copy(
                            pending = WorkingModePendingCommand.None,
                            automaticModeActive = previousAutomatic,
                            knownMode = previousKnown
                        ),
                        listOf(WorkingModeEffect.RequestStatusRefresh)
                    )
                }
            }
        }
    }
}
