package com.blelock.app

import org.junit.Assert.assertEquals
import org.junit.Test

class InternetOpenProgressButtonTest {
    @Test
    fun mapsConfirmedBrokerResultsToFiveProgressStages() {
        assertEquals(InternetOpenStage.UNLOCKED, internetOpenStageForReply("opened", 0))
        assertEquals(InternetOpenStage.CLOSING, internetOpenStageForReply("closing", 0))
        assertEquals(
            InternetOpenStage.UNLOCKED,
            internetOpenStageForReply("closing", 0, "closed", lockWasOpened = true)
        )
        assertEquals(
            InternetOpenStage.CLOSING,
            internetOpenStageForReply("closing", 0, "closed", lockWasOpened = true, doorWasOpened = true)
        )
        assertEquals(InternetOpenStage.CLOSED, internetOpenStageForReply("closed", 0))
        assertEquals(InternetOpenStage.CLOSED, internetOpenStageForReply("closed", 0, "closed"))
        assertEquals(
            InternetOpenStage.CLOSED_WITHOUT_DOOR_OPEN,
            internetOpenStageForReply("closed", 0, "closed", lockWasOpened = true)
        )
        assertEquals(
            InternetOpenStage.CLOSED,
            internetOpenStageForReply("closed", 0, "closed", lockWasOpened = true, doorWasOpened = true)
        )
        assertEquals(
            InternetOpenStage.CLOSED,
            internetOpenStageForReply("closed", 0, "unknown", lockWasOpened = true)
        )
        assertEquals(InternetOpenStage.CLOSED, internetOpenStageForReply("closed", 0, "unknown"))
        assertEquals(InternetOpenStage.ERROR, internetOpenStageForReply("closed", 0, "open"))
    }

    @Test
    fun faultsAndFailureResultsMapToError() {
        assertEquals(InternetOpenStage.ERROR, internetOpenStageForReply("opened", 1))
        assertEquals(InternetOpenStage.ERROR, internetOpenStageForReply("closed", 1))
        assertEquals(InternetOpenStage.ERROR, internetOpenStageForReply("error", 0))
        assertEquals(InternetOpenStage.ERROR, internetOpenStageForReply("interrupted", 0))
    }
}
