package com.blelock.app

/** Tracks lock confirmations after an accepted automatic OPEN write. */
internal class AutoOpenPresenceGate {
    private var awaitingLockCycle = false
    private var openConfirmed = false

    fun onOpenCommandAccepted() {
        awaitingLockCycle = true
        openConfirmed = false
    }

    /** Returns true once the lock has confirmed OPEN and then LOCKED. */
    fun onLockStatus(status: String?): Boolean {
        if (!awaitingLockCycle) return false
        when (status?.trim()?.uppercase()) {
            "OPEN" -> openConfirmed = true
            "LOCKED" -> if (openConfirmed) {
                awaitingLockCycle = false
                openConfirmed = false
                return true
            }
        }
        return false
    }
}
