package com.blelock.app

/** Allows one automatic OPEN until BLE departure is confirmed. */
internal class AutoOpenPresenceGate {
    var isBlocked = false
        private set

    fun onOpenCommandAccepted() {
        isBlocked = true
    }

    fun onDepartureConfirmed() {
        isBlocked = false
    }
}
