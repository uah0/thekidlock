package com.blelock.app

import org.junit.Assert.assertEquals
import org.junit.Test

class InternetOpenProgressButtonTest {
    @Test
    fun closeCountdownRoundsUpAndStopsAtZero() {
        assertEquals(20, internetCloseCountdownSeconds(19_001))
        assertEquals(1, internetCloseCountdownSeconds(1))
        assertEquals(0, internetCloseCountdownSeconds(0))
        assertEquals(0, internetCloseCountdownSeconds(-1))
    }

    @Test
    fun closingAndErrorsDoNotInventAnotherVisibleStage() {
        assertEquals(InternetOpenStage.UNLOCKED,
            internetOpenDisplayedStage(InternetOpenStage.UNLOCKED, InternetOpenStage.CLOSING))
        assertEquals(InternetOpenStage.DOOR_OPEN,
            internetOpenDisplayedStage(InternetOpenStage.DOOR_OPEN, InternetOpenStage.CLOSING))
        assertEquals(InternetOpenStage.BROKER_ACCEPTED,
            internetOpenDisplayedStage(InternetOpenStage.BROKER_ACCEPTED, InternetOpenStage.ERROR))
        assertEquals(InternetOpenStage.CLOSED,
            internetOpenDisplayedStage(InternetOpenStage.DOOR_OPEN, InternetOpenStage.CLOSED))
    }

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
    fun doorOpeningRequiresSignedDoorEvidenceAndNoFaults() {
        assertEquals(InternetOpenStage.DOOR_OPEN, internetOpenStageForReply("opened", 0, "open"))
        assertEquals(InternetOpenStage.DOOR_OPEN, internetOpenStageForReply("closing", 0, "open"))
        assertEquals(InternetOpenStage.CLOSING, internetOpenStageForReply("closing", 0, "unknown"))
        assertEquals(InternetOpenStage.ERROR, internetOpenStageForReply("closing", 1, "open"))
        assertEquals(InternetOpenStage.ERROR, internetOpenStageForReply("closing", 1, "closed"))
    }

    @Test
    fun faultsAndFailureResultsMapToError() {
        assertEquals(InternetOpenStage.ERROR, internetOpenStageForReply("opened", 1))
        assertEquals(InternetOpenStage.ERROR, internetOpenStageForReply("closed", 1))
        assertEquals(InternetOpenStage.ERROR, internetOpenStageForReply("error", 0))
        assertEquals(InternetOpenStage.ERROR, internetOpenStageForReply("interrupted", 0))
    }
}
