package com.blelock.app

/** Commands exposed by the single BLE owner to the UI. */
interface BleClient {
    val currentState: BleRepository.State
    fun requestLockStatusRefresh(): Boolean
    fun findAndConnect()
    fun pollAdminState()
    fun retryAdminProbe()
    fun readAdminConfig()
    fun readAdminSchema()
    fun writeAdminConfig(value: String)
    fun readAdminBonds()
    fun writeCurrentPhoneName()
    fun readOtaConfig()
    fun readOtaStatus(): Boolean
    fun writeOtaConfig(value: String)
    fun startHomeOta(): Boolean
    fun deleteBond(address: String)
    fun deleteAllBonds()
    fun rebootEsp32(): Boolean
    fun setAutomaticMode(enabled: Boolean)
    fun openNow()
    fun forceOpenNow()
    fun toggleKeepOpen()
    fun setWorkingMode(alwaysOpen: Boolean, closeWhenDisablingAlwaysOpen: Boolean = false)
    fun close()
}
