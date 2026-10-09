package com.blelock.app

import android.annotation.SuppressLint
import android.app.Activity
import android.app.Dialog
import android.graphics.Color
import android.graphics.Bitmap
import android.os.Bundle
import android.os.Build
import android.view.KeyEvent
import android.view.ViewGroup
import android.view.WindowInsets
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.LinearLayout
import java.io.ByteArrayInputStream
import java.io.IOException

/** Hosts the existing ESP page; the private encrypted image stays on the phone. */
internal class LockWebUiDialog(activity: Activity, private val url: String) :
    Dialog(activity, android.R.style.Theme_Material_Light_NoActionBar) {
    private val policy = WebUiRequestPolicy(url)
    private lateinit var webView: WebView
    private lateinit var wizardSpeech: LockWizardSpeech

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val layout = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(Color.WHITE)
            setOnApplyWindowInsetsListener { view, insets ->
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                    val safeArea = insets.getInsets(
                        WindowInsets.Type.systemBars() or WindowInsets.Type.displayCutout())
                    view.setPadding(safeArea.left, safeArea.top, safeArea.right, safeArea.bottom)
                } else {
                    @Suppress("DEPRECATION")
                    view.setPadding(insets.systemWindowInsetLeft, insets.systemWindowInsetTop,
                        insets.systemWindowInsetRight, insets.systemWindowInsetBottom)
                }
                insets
            }
            post { requestApplyInsets() }
        }
        webView = WebView(context).apply {
            settings.javaScriptEnabled = true
            settings.domStorageEnabled = true
            settings.allowFileAccess = false
            settings.allowContentAccess = false
            settings.mixedContentMode = android.webkit.WebSettings.MIXED_CONTENT_NEVER_ALLOW
            settings.setSupportMultipleWindows(false)
            webViewClient = object : WebViewClient() {
                override fun onPageStarted(view: WebView?, url: String?, favicon: Bitmap?) {
                    wizardSpeech.stop()
                }
                override fun shouldOverrideUrlLoading(view: WebView?, request: WebResourceRequest): Boolean =
                    !policy.allows(request.url.toString())

                override fun shouldInterceptRequest(view: WebView?, request: WebResourceRequest): WebResourceResponse? {
                    val requestedUrl = request.url.toString()
                    if (!policy.allows(requestedUrl)) return textResponse(403, "Forbidden", "Адрес недоступен")
                    if (request.url.path != WebUiRequestPolicy.FIRMWARE_PATH) return null
                    if (!policy.isFirmwareRequest(requestedUrl, request.method) || request.isForMainFrame) {
                        return textResponse(403, "Forbidden", "Адрес недоступен")
                    }
                    return try {
                        val input = FirmwareUpdateClient.openStoredFirmware(context.applicationContext)
                        WebResourceResponse("application/octet-stream", null, 200, "OK",
                            mapOf("Cache-Control" to "no-store", "Content-Length" to input.channel.size().toString()), input)
                    } catch (_: IOException) {
                        textResponse(404, "Not Found", "Прошивка ещё не скачана. Подключите телефон к интернету и откройте приложение.")
                    }
                }

                override fun onPageFinished(view: WebView, finishedUrl: String) {
                    if (policy.allows(finishedUrl) && policy.allows(view.url.orEmpty())) {
                        view.evaluateJavascript("window.bleLockInternalFirmware=true;" +
                            "if(typeof useInternalFirmware==='function')useInternalFirmware();", null)
                        if (policy.isCalibrationPage(finishedUrl) && policy.isCalibrationPage(view.url.orEmpty())) {
                            val script = context.assets.open("lock_wizard_speech.js").bufferedReader().use { it.readText() }
                            view.evaluateJavascript(script, null)
                        }
                    }
                }
            }
        }
        wizardSpeech = LockWizardSpeech(context, webView, policy)
        webView.addJavascriptInterface(wizardSpeech, "BleLockWizardSpeech")
        if (BuildConfig.DEBUG) WebView.setWebContentsDebuggingEnabled(true)
        layout.addView(webView, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
        setContentView(layout)
        window?.setLayout(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
        setOnKeyListener { _, keyCode, event ->
            if (keyCode != KeyEvent.KEYCODE_BACK) false else {
                if (event.action == KeyEvent.ACTION_UP) goBack()
                true
            }
        }
        webView.loadUrl(url)
    }

    private fun goBack() {
        if (webView.canGoBack()) webView.goBack() else dismiss()
    }

    override fun onStop() {
        wizardSpeech.close()
        webView.removeJavascriptInterface("BleLockWizardSpeech")
        webView.stopLoading()
        webView.destroy()
        super.onStop()
    }

    private fun textResponse(status: Int, reason: String, message: String) =
        WebResourceResponse("text/plain", "UTF-8", status, reason,
            mapOf("Cache-Control" to "no-store"), ByteArrayInputStream(message.toByteArray(Charsets.UTF_8)))
}
