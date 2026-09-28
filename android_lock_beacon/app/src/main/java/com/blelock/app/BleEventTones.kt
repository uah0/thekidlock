package com.blelock.app

import android.media.AudioManager
import android.media.ToneGenerator
import android.os.Handler
import android.os.Looper
import android.os.SystemClock

/** Short, distinct audio cues for the main BLE connection lifecycle events. */
internal object BleEventTones {
    private val handler = Handler(Looper.getMainLooper())
    private var toneGenerator: ToneGenerator? = null
    private var releaseRunnable: Runnable? = null
    private var lastWeakAdvertisementAtMs: Long? = null

    fun advertisementDetected() = play(ToneGenerator.TONE_PROP_BEEP, 90)

    @Synchronized
    fun weakAdvertisement() {
        val nowMs = SystemClock.elapsedRealtime()
        if (lastWeakAdvertisementAtMs?.let { nowMs - it < WEAK_ADVERTISEMENT_DEBOUNCE_MS } == true) {
            return
        }
        lastWeakAdvertisementAtMs = nowMs
        play(ToneGenerator.TONE_PROP_BEEP2, 70)
    }

    fun connected() = play(ToneGenerator.TONE_PROP_ACK, 150)

    fun disconnected() = play(ToneGenerator.TONE_PROP_NACK, 180)

    @Synchronized
    private fun play(tone: Int, durationMs: Int) {
        handler.post {
            synchronized(this) {
                releaseRunnable?.let(handler::removeCallbacks)
                releaseRunnable = null
                val generator = toneGenerator ?: runCatching {
                    ToneGenerator(AudioManager.STREAM_NOTIFICATION, 80)
                }.getOrNull()?.also { toneGenerator = it } ?: return@synchronized

                generator.startTone(tone, durationMs)
                val cleanup = Runnable {
                    synchronized(this) {
                        generator.stopTone()
                        generator.release()
                        if (toneGenerator === generator) toneGenerator = null
                        releaseRunnable = null
                    }
                }
                releaseRunnable = cleanup
                handler.postDelayed(cleanup, durationMs.toLong() + 100L)
            }
        }
    }

    private const val WEAK_ADVERTISEMENT_DEBOUNCE_MS = 1_500L
}
