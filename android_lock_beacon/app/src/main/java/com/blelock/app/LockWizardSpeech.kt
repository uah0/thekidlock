package com.blelock.app

import android.content.Context
import android.media.AudioAttributes
import android.os.Handler
import android.os.Looper
import android.speech.tts.TextToSpeech
import android.speech.tts.UtteranceProgressListener
import android.util.Log
import android.webkit.JavascriptInterface
import android.webkit.WebView
import org.json.JSONObject
import java.util.Locale

/** Only speech operations are exposed; callbacks are scoped to the current wizard. */
internal class LockWizardSpeech(context: Context, private val view: WebView,
    private val policy: WebUiRequestPolicy) {
    private data class Request(val text: String, val token: Int, val id: String)
    private val handler = Handler(Looper.getMainLooper())
    private var engine: TextToSpeech? = null
    private var initialized = false
    private var available = false
    private var closed = false
    private var serial = 0L
    private var request: Request? = null

    init {
        engine = TextToSpeech(context.applicationContext) { result ->
            handler.post {
                if (!closed) {
                    initialized = true
                    val tts = engine
                    available = result == TextToSpeech.SUCCESS && tts != null &&
                        tts.setLanguage(Locale.forLanguageTag("ru-RU")) >= TextToSpeech.LANG_AVAILABLE
                    if (available) {
                        tts?.setAudioAttributes(AudioAttributes.Builder()
                            .setUsage(AudioAttributes.USAGE_MEDIA)
                            .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build())
                        tts?.setOnUtteranceProgressListener(object : UtteranceProgressListener() {
                            override fun onStart(utteranceId: String?) {
                                Log.i(TAG, "Wizard speech started: $utteranceId")
                            }
                            override fun onDone(utteranceId: String?) = finish(utteranceId, null)
                            override fun onError(utteranceId: String?) =
                                finish(utteranceId, "Не удалось озвучить инструкцию")
                            override fun onError(utteranceId: String?, errorCode: Int) =
                                finish(utteranceId, "Не удалось озвучить инструкцию ($errorCode)")
                        })
                    }
                    Log.i(TAG, "Russian TTS available=$available")
                    request?.let { play(it) }
                }
            }
        }
    }

    @JavascriptInterface
    fun speak(text: String, token: Int) {
        handler.post {
            if (closed || !policy.isCalibrationPage(view.url.orEmpty())) return@post
            request = null
            engine?.stop()
            val next = Request(text, token, "${++serial}:$token")
            request = next
            if (text.isBlank() || text.length > 2000) {
                finish(next.id, "Недопустимый текст инструкции")
            } else if (initialized) play(next)
        }
    }

    @JavascriptInterface
    fun cancel() { handler.post { stop("page cancellation") } }

    fun stop(reason: String = "navigation") {
        request?.let { Log.i(TAG, "Wizard speech cancelled: ${it.id}; reason=$reason") }
        request = null
        engine?.stop()
    }

    fun close() {
        closed = true
        stop("window closed")
        engine?.shutdown()
        engine = null
    }

    private fun play(next: Request) {
        if (!available) {
            finish(next.id, "Русская озвучка недоступна. Проверьте синтезатор речи в настройках Android")
        } else if (engine?.speak(next.text, TextToSpeech.QUEUE_FLUSH, null, next.id) != TextToSpeech.SUCCESS) {
            finish(next.id, "Не удалось запустить озвучивание")
        }
    }

    private fun finish(id: String?, error: String?) {
        handler.post {
            val current = request
            if (closed || current == null || current.id != id) return@post
            request = null
            if (!policy.isCalibrationPage(view.url.orEmpty())) return@post
            Log.i(TAG, "Wizard speech finished: $id; success=${error == null}")
            val message = if (error == null) "null" else JSONObject.quote(error)
            view.evaluateJavascript("if(typeof onBleLockSpeechDone==='function')" +
                "onBleLockSpeechDone(${current.token},$message);", null)
        }
    }

    companion object { private const val TAG = "LockWizardSpeech" }
}
