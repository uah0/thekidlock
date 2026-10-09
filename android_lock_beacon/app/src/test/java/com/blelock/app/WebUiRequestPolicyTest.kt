package com.blelock.app

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class WebUiRequestPolicyTest {
    private val policy = WebUiRequestPolicy("http://172.18.51.177/")

    @Test
    fun nativeSpeechBelongsOnlyToTheCurrentEspWizard() {
        assertTrue(policy.isCalibrationPage("http://172.18.51.177/calibration"))
        assertFalse(policy.isCalibrationPage("http://172.18.51.178/calibration"))
        assertFalse(policy.isCalibrationPage("http://172.18.51.177/"))
        assertFalse(policy.isCalibrationPage("file:///calibration"))
    }

    @Test
    fun privateImageIsAvailableOnlyOnTheSelectedEspOrigin() {
        assertTrue(policy.isFirmwareRequest("http://172.18.51.177/__app/firmware", "GET"))
        assertFalse(policy.isFirmwareRequest("http://172.18.51.178/__app/firmware", "GET"))
        assertFalse(policy.isFirmwareRequest("https://172.18.51.177/__app/firmware", "GET"))
        assertFalse(policy.isFirmwareRequest("http://172.18.51.177:8080/__app/firmware", "GET"))
        assertFalse(policy.isFirmwareRequest("http://user@172.18.51.177/__app/firmware", "GET"))
        assertFalse(policy.isFirmwareRequest("http://172.18.51.177/__app/firmware?file=other", "GET"))
        assertFalse(policy.isFirmwareRequest("http://172.18.51.177/__app/firmware", "POST"))
    }

    @Test
    fun calibrationWorksButExternalAndLocalFileNavigationAreBlocked() {
        assertTrue(policy.allows("http://172.18.51.177/calibration"))
        assertFalse(policy.allows("https://github.com/uah0/thekidlock"))
        assertFalse(policy.allows("file:///data/user/0/com.blelock.app/files/firmware/ble_lock_stage1_s3.bin"))
        assertFalse(policy.allows("content://com.blelock.app/firmware"))
        assertFalse(policy.allows("javascript:alert(1)"))
    }
}
