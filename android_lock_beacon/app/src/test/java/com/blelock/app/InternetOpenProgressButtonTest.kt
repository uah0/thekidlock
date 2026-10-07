package com.blelock.app

import org.junit.Assert.assertEquals
import org.junit.Test

class InternetOpenProgressButtonTest {
    @Test
    fun mapsConfirmedBrokerResultsToFiveProgressStages() {
        assertEquals(InternetOpenStage.UNLOCKED, internetOpenStageForReply("opened", 0))
        assertEquals(InternetOpenStage.CLOSING, internetOpenStageForReply("closing", 0))
        assertEquals(InternetOpenStage.CLOSED, internetOpenStageForReply("closed", 0))
    }

    @Test
    fun faultsAndFailureResultsMapToError() {
        assertEquals(InternetOpenStage.ERROR, internetOpenStageForReply("opened", 1))
        assertEquals(InternetOpenStage.ERROR, internetOpenStageForReply("closed", 1))
        assertEquals(InternetOpenStage.ERROR, internetOpenStageForReply("error", 0))
        assertEquals(InternetOpenStage.ERROR, internetOpenStageForReply("interrupted", 0))
    }
}
