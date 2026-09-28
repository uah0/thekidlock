package com.blelock.app

enum class ConfirmedLockMode {
    UNKNOWN,
    AUTO,
    ALWAYS_OPEN
}

sealed class WorkingModeTransition {
    data class ToAlwaysOpen(
        val requestId: Long
    ) : WorkingModeTransition()

    data class ToAuto(
        val requestId: Long,
        val activateAutomaticService: Boolean,
        val lockModeConfirmed: Boolean = false,
        val resumeRequested: Boolean = false
    ) : WorkingModeTransition()

    data class AutomaticServiceToActive(
        val requestId: Long,
        val lockModeConfirmed: Boolean
    ) : WorkingModeTransition()

    data class AutomaticServiceToInactive(val requestId: Long) : WorkingModeTransition()
}

data class WorkingModeState(
    val bleState: BleRepository.State = BleRepository.State.IDLE,
    val confirmedLockMode: ConfirmedLockMode = ConfirmedLockMode.UNKNOWN,
    val confirmedAutomaticServiceActive: Boolean? = null,
    val transition: WorkingModeTransition? = null
)

data class WorkingModeButtonsUi(
    val autoSelected: Boolean,
    val alwaysOpenSelected: Boolean,
    val enabled: Boolean,
    val transitionInProgress: Boolean
)

object WorkingModeUi {
    fun compute(state: WorkingModeState): WorkingModeButtonsUi = WorkingModeButtonsUi(
        autoSelected = state.confirmedLockMode == ConfirmedLockMode.AUTO &&
            state.confirmedAutomaticServiceActive == true,
        alwaysOpenSelected = state.confirmedLockMode == ConfirmedLockMode.ALWAYS_OPEN,
        enabled = state.bleState == BleRepository.State.READY && state.transition == null,
        transitionInProgress = state.transition != null
    )
}

sealed class WorkingModeEvent {
    data class BleStateSynced(val state: BleRepository.State) : WorkingModeEvent()
    object ConnectionLost : WorkingModeEvent()
    object AutomaticServiceUnavailable : WorkingModeEvent()
    object WorkingModeCommandFailed : WorkingModeEvent()
    data class LockStatusReported(val status: String) : WorkingModeEvent()
    data class AutomaticModeConfirmed(val active: Boolean) : WorkingModeEvent()
    object UserToggledAuto : WorkingModeEvent()
    object UserToggledAlwaysOpen : WorkingModeEvent()
    data class TransitionTimedOut(val requestId: Long) : WorkingModeEvent()
}

sealed class WorkingModeEffect {
    data class SendSetAutomaticMode(val enabled: Boolean) : WorkingModeEffect()
    data class SendSetWorkingMode(
        val alwaysOpen: Boolean,
        val persistAutoOpen: Boolean = false,
        val closeWhenDisablingAlwaysOpen: Boolean = false
    ) : WorkingModeEffect()
    object ResumeAutomaticMode : WorkingModeEffect()
    data class PersistAutoOpen(val enabled: Boolean) : WorkingModeEffect()
    data class ScheduleTimeout(val requestId: Long, val atMs: Long) : WorkingModeEffect()
    object CancelTimeout : WorkingModeEffect()
    object RequestStatusRefresh : WorkingModeEffect()
}

data class WorkingModeReduceResult(
    val state: WorkingModeState,
    val effects: List<WorkingModeEffect> = emptyList()
)

object WorkingModeReducer {
    const val COMMAND_TIMEOUT_MS = 12_000L

    fun reduce(
        state: WorkingModeState,
        event: WorkingModeEvent,
        nowMs: Long,
        autoOpenEnabled: Boolean = false
    ): WorkingModeReduceResult = when (event) {
        is WorkingModeEvent.BleStateSynced -> {
            val disconnected = event.state == BleRepository.State.IDLE ||
                event.state == BleRepository.State.ERROR ||
                event.state == BleRepository.State.DEPARTED
            if (disconnected) {
                WorkingModeReduceResult(
                    state.copy(
                        bleState = event.state,
                        confirmedLockMode = ConfirmedLockMode.UNKNOWN,
                        transition = null
                    ),
                    if (state.transition != null) listOf(WorkingModeEffect.CancelTimeout) else emptyList()
                )
            } else {
                WorkingModeReduceResult(state.copy(bleState = event.state))
            }
        }

        WorkingModeEvent.ConnectionLost -> WorkingModeReduceResult(
            state.copy(
                bleState = BleRepository.State.ERROR,
                confirmedLockMode = ConfirmedLockMode.UNKNOWN,
                transition = null
            ),
            if (state.transition != null) listOf(WorkingModeEffect.CancelTimeout) else emptyList()
        )

        WorkingModeEvent.AutomaticServiceUnavailable -> WorkingModeReduceResult(
            state.copy(
                bleState = BleRepository.State.ERROR,
                confirmedLockMode = ConfirmedLockMode.UNKNOWN,
                confirmedAutomaticServiceActive = null,
                transition = null
            ),
            if (state.transition != null) listOf(WorkingModeEffect.CancelTimeout) else emptyList()
        )

        WorkingModeEvent.WorkingModeCommandFailed -> WorkingModeReduceResult(
            state.copy(bleState = BleRepository.State.ERROR, transition = null),
            listOf(WorkingModeEffect.CancelTimeout, WorkingModeEffect.RequestStatusRefresh)
        )

        is WorkingModeEvent.LockStatusReported -> onLockStatus(state, event.status)

        is WorkingModeEvent.AutomaticModeConfirmed -> onAutomaticMode(state, event.active)

        WorkingModeEvent.UserToggledAuto -> {
            if (state.bleState != BleRepository.State.READY || state.transition != null) {
                WorkingModeReduceResult(state)
            } else when (state.confirmedLockMode) {
                ConfirmedLockMode.UNKNOWN -> WorkingModeReduceResult(
                    state,
                    listOf(WorkingModeEffect.RequestStatusRefresh)
                )
                ConfirmedLockMode.ALWAYS_OPEN -> {
                    val transition = WorkingModeTransition.ToAuto(
                        requestId = nowMs,
                        activateAutomaticService = true
                    )
                    WorkingModeReduceResult(
                        state.copy(transition = transition),
                        listOf(
                            WorkingModeEffect.PersistAutoOpen(true),
                            WorkingModeEffect.SendSetWorkingMode(
                                alwaysOpen = false,
                                persistAutoOpen = true,
                                closeWhenDisablingAlwaysOpen = true
                            ),
                            WorkingModeEffect.ScheduleTimeout(nowMs, nowMs + COMMAND_TIMEOUT_MS)
                        )
                    )
                }
                ConfirmedLockMode.AUTO -> if (state.confirmedAutomaticServiceActive == true) {
                    WorkingModeReduceResult(
                        state.copy(transition = WorkingModeTransition.AutomaticServiceToInactive(nowMs)),
                        listOf(
                            WorkingModeEffect.PersistAutoOpen(false),
                            WorkingModeEffect.SendSetAutomaticMode(false),
                            WorkingModeEffect.ScheduleTimeout(nowMs, nowMs + COMMAND_TIMEOUT_MS)
                        )
                    )
                } else {
                    WorkingModeReduceResult(
                        state.copy(
                            transition = WorkingModeTransition.AutomaticServiceToActive(
                                nowMs,
                                lockModeConfirmed = true
                            )
                        ),
                        listOf(
                            WorkingModeEffect.PersistAutoOpen(true),
                            WorkingModeEffect.SendSetAutomaticMode(true),
                            WorkingModeEffect.ScheduleTimeout(nowMs, nowMs + COMMAND_TIMEOUT_MS)
                        )
                    )
                }
            }
        }

        WorkingModeEvent.UserToggledAlwaysOpen -> {
            if (state.bleState != BleRepository.State.READY || state.transition != null) {
                WorkingModeReduceResult(state)
            } else when (state.confirmedLockMode) {
                ConfirmedLockMode.UNKNOWN -> WorkingModeReduceResult(
                    state,
                    listOf(WorkingModeEffect.RequestStatusRefresh)
                )
                ConfirmedLockMode.AUTO -> {
                    val transition = WorkingModeTransition.ToAlwaysOpen(nowMs)
                    WorkingModeReduceResult(
                        state.copy(transition = transition),
                        listOf(
                            WorkingModeEffect.SendSetWorkingMode(alwaysOpen = true),
                            WorkingModeEffect.ScheduleTimeout(nowMs, nowMs + COMMAND_TIMEOUT_MS)
                        )
                    )
                }
                ConfirmedLockMode.ALWAYS_OPEN -> {
                    val transition = WorkingModeTransition.ToAuto(
                        requestId = nowMs,
                        activateAutomaticService = false
                    )
                    WorkingModeReduceResult(
                        state.copy(transition = transition),
                        listOf(
                            WorkingModeEffect.PersistAutoOpen(false),
                            WorkingModeEffect.SendSetWorkingMode(
                                alwaysOpen = false,
                                closeWhenDisablingAlwaysOpen = true
                            ),
                            WorkingModeEffect.SendSetAutomaticMode(false),
                            WorkingModeEffect.ScheduleTimeout(nowMs, nowMs + COMMAND_TIMEOUT_MS)
                        )
                    )
                }
            }
        }

        is WorkingModeEvent.TransitionTimedOut -> {
            if (state.transition?.requestId() != event.requestId) {
                WorkingModeReduceResult(state)
            } else {
                WorkingModeReduceResult(
                    state.copy(transition = null),
                    listOf(WorkingModeEffect.CancelTimeout, WorkingModeEffect.RequestStatusRefresh)
                )
            }
        }
    }

    private fun onLockStatus(
        state: WorkingModeState,
        rawStatus: String
    ): WorkingModeReduceResult {
        val mode = when (rawStatus.trim().uppercase()) {
            "OPEN", "LOCKED" -> ConfirmedLockMode.AUTO
            "OPEN_HOLD" -> ConfirmedLockMode.ALWAYS_OPEN
            else -> return WorkingModeReduceResult(state)
        }
        var next = state.copy(confirmedLockMode = mode)
        val effects = mutableListOf<WorkingModeEffect>()
        when (val transition = state.transition) {
            is WorkingModeTransition.ToAlwaysOpen -> if (mode == ConfirmedLockMode.ALWAYS_OPEN) {
                next = next.copy(transition = null)
                effects += WorkingModeEffect.CancelTimeout
            }
            is WorkingModeTransition.ToAuto -> if (mode == ConfirmedLockMode.AUTO) {
                if (!transition.activateAutomaticService || state.confirmedAutomaticServiceActive == true) {
                    next = next.copy(transition = null)
                    effects += WorkingModeEffect.CancelTimeout
                } else {
                    val updated = transition.copy(lockModeConfirmed = true)
                    next = next.copy(transition = updated)
                    if (!updated.resumeRequested) {
                        next = next.copy(transition = updated.copy(resumeRequested = true))
                        effects += WorkingModeEffect.ResumeAutomaticMode
                    }
                }
            }
            is WorkingModeTransition.AutomaticServiceToActive -> if (mode == ConfirmedLockMode.AUTO) {
                val updated = transition.copy(lockModeConfirmed = true)
                if (state.confirmedAutomaticServiceActive == true) {
                    next = next.copy(transition = null)
                    effects += WorkingModeEffect.CancelTimeout
                } else {
                    next = next.copy(transition = updated)
                }
            }
            else -> Unit
        }
        return WorkingModeReduceResult(next, effects)
    }

    private fun onAutomaticMode(
        state: WorkingModeState,
        active: Boolean
    ): WorkingModeReduceResult {
        var next = state.copy(confirmedAutomaticServiceActive = active)
        val transition = state.transition
        if (transition is WorkingModeTransition.AutomaticServiceToInactive && !active) {
            next = next.copy(transition = null)
            return WorkingModeReduceResult(next, listOf(WorkingModeEffect.CancelTimeout))
        }
        if (transition is WorkingModeTransition.AutomaticServiceToActive && active &&
            transition.lockModeConfirmed
        ) {
            next = next.copy(transition = null)
            return WorkingModeReduceResult(next, listOf(WorkingModeEffect.CancelTimeout))
        }
        if (transition is WorkingModeTransition.ToAuto && active && transition.lockModeConfirmed) {
            next = next.copy(transition = null)
            return WorkingModeReduceResult(next, listOf(WorkingModeEffect.CancelTimeout))
        }
        return WorkingModeReduceResult(next)
    }

    private fun WorkingModeTransition.requestId(): Long = when (this) {
        is WorkingModeTransition.ToAlwaysOpen -> requestId
        is WorkingModeTransition.ToAuto -> requestId
        is WorkingModeTransition.AutomaticServiceToActive -> requestId
        is WorkingModeTransition.AutomaticServiceToInactive -> requestId
    }
}
