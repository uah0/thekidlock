package com.blelock.app

import android.media.AudioManager
import android.media.ToneGenerator
import android.content.Context
import android.os.PowerManager
import android.os.Handler
import android.os.Looper
import android.util.Log

/** Short, distinct audio cues for the main BLE connection lifecycle events. */
internal object BleEventTones {
    private val handler = Handler(Looper.getMainLooper())
    private var toneGenerator: ToneGenerator? = null
    private var wakeLock: PowerManager.WakeLock? = null
    private var releaseRunnable: Runnable? = null

    fun advertisementDetected(context: Context) =
        play(context, ToneGenerator.TONE_PROP_BEEP, 90)

    @Synchronized
    private fun play(context: Context, tone: Int, durationMs: Int) {
        handler.post {
            synchronized(this) {
                releaseRunnable?.let(handler::removeCallbacks)
                releaseRunnable = null
                releaseWakeLock()
                val powerManager = context.applicationContext
                    .getSystemService(Context.POWER_SERVICE) as? PowerManager
                wakeLock = powerManager?.newWakeLock(
                    PowerManager.PARTIAL_WAKE_LOCK,
                    "BleLock:AdvertisementTone"
                )?.also { it.acquire((durationMs + 1_000L)) }
                val generator = toneGenerator ?: runCatching {
                    ToneGenerator(AudioManager.STREAM_NOTIFICATION, 80)
                }.getOrNull()?.also { toneGenerator = it } ?: run {
                    Log.e(TAG, "Unable to create ToneGenerator")
                    releaseWakeLock()
                    return@synchronized
                }

                val started = generator.startTone(tone, durationMs)
                Log.i(TAG, "Advertisement tone started=$started screenOn=${powerManager?.isInteractive}")
                val cleanup = Runnable {
                    synchronized(this) {
                        generator.stopTone()
                        generator.release()
                        if (toneGenerator === generator) toneGenerator = null
                        releaseRunnable = null
                        releaseWakeLock()
                    }
                }
                releaseRunnable = cleanup
                handler.postDelayed(cleanup, durationMs.toLong() + 100L)
            }
        }
    }

    private fun releaseWakeLock() {
        wakeLock?.let { lock ->
            if (lock.isHeld) runCatching { lock.release() }
        }
        wakeLock = null
    }

    private const val TAG = "BleEventTone"
}
