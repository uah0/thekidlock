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
import android.webkit.JavascriptInterface
import android.widget.LinearLayout
import java.io.ByteArrayInputStream
import java.io.IOException

/** Hosts the existing ESP page; the private encrypted image stays on the phone. */
internal class LockWebUiDialog(private val activity: Activity, private val url: String,
    private val lockAddress: String? = FirmwareTransferState.currentLock(activity),
    private val onTransferStarted: () -> Unit = {}) :
    Dialog(activity, android.R.style.Theme_Material_Light_NoActionBar) {
    private val policy = WebUiRequestPolicy(url)
    private lateinit var webView: WebView
    private lateinit var wizardSpeech: LockWizardSpeech
    @Volatile private var servedVersion: String? = null
    @Volatile private var trustedPage = false

    private inner class TransferBridge {
        @JavascriptInterface
        fun started(version: String): Boolean {
            if (!trustedPage || version != servedVersion) return false
            if (!FirmwareTransferState.markStarted(context, lockAddress, version)) return false
            activity.runOnUiThread { onTransferStarted() }
            return true
        }
    }

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
                    trustedPage = false
                    wizardSpeech.stop()
                }
                override fun shouldOverrideUrlLoading(view: WebView?, request: WebResourceRequest): Boolean =
                    !policy.allows(request.url.toString())

                override fun shouldInterceptRequest(view: WebView?, request: WebResourceRequest): WebResourceResponse? {
                    val requestedUrl = request.url.toString()
                    if (!policy.allows(requestedUrl)) return textResponse(403, "Forbidden", "WUI_E_FORBIDDEN")
                    if (request.url.path != WebUiRequestPolicy.FIRMWARE_PATH) return null
                    if (!policy.isFirmwareRequest(requestedUrl, request.method) || request.isForMainFrame) {
                        return textResponse(403, "Forbidden", "WUI_E_FORBIDDEN")
                    }
                    return try {
                        synchronized(FirmwareUpdateClient) {
                            val input = FirmwareUpdateClient.openStoredFirmware(context.applicationContext)
                            val version = FirmwareTransferState.downloadedVersion(context.applicationContext)
                            servedVersion = version
                            WebResourceResponse("application/octet-stream", null, 200, "OK",
                                mapOf("Cache-Control" to "no-store", "Content-Length" to input.channel.size().toString(),
                                    "X-Firmware-Version" to version.orEmpty()), input)
                        }
                    } catch (_: IOException) {
                        textResponse(404, "Not Found", "WUI_E_FIRMWARE_MISSING")
                    }
                }

                override fun onPageFinished(view: WebView, finishedUrl: String) {
                    if (policy.allows(finishedUrl) && policy.allows(view.url.orEmpty())) {
                        trustedPage = true
                        val calibrationPage = policy.isCalibrationPage(finishedUrl) &&
                            policy.isCalibrationPage(view.url.orEmpty())
                        val language = AppLanguage.currentTag(context)
                        val translationJson = context.assets.open("webui/i18n/$language.json")
                            .bufferedReader().use { org.json.JSONObject(it.readText()).toString() }
                        val installTranslations = "window.installAppWebUiTranslations(JSON.parse(" +
                            org.json.JSONObject.quote(translationJson) + "));"
                        view.evaluateJavascript(installTranslations) {
                            if (!trustedPage || !policy.allows(view.url.orEmpty())) return@evaluateJavascript
                            if (calibrationPage) {
                                val speechScript = context.assets.open("lock_wizard_speech.js")
                                    .bufferedReader().use { it.readText() }
                                view.evaluateJavascript(speechScript) {
                                    initializeLocalizedPage(view)
                                }
                            } else {
                                view.evaluateJavascript(
                                    "window.bleLockTransferError='WUI_E_TRANSFER_START';",
                                    null
                                )
                                val transferScript = context.assets.open("firmware_transfer.js")
                                    .bufferedReader().use { it.readText() }
                                view.evaluateJavascript(transferScript) {
                                    view.evaluateJavascript(
                                        "window.bleLockInternalFirmware=true;" +
                                            "if(typeof useInternalFirmware==='function')useInternalFirmware();",
                                        null
                                    )
                                    initializeLocalizedPage(view)
                                }
                            }
                        }
                    }
                }
            }
        }
        wizardSpeech = LockWizardSpeech(context, webView, policy)
        webView.addJavascriptInterface(wizardSpeech, "BleLockWizardSpeech")
        webView.addJavascriptInterface(TransferBridge(), "BleLockFirmwareTransfer")
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

    private fun initializeLocalizedPage(view: WebView) {
        if (!trustedPage || !policy.allows(view.url.orEmpty())) return
        view.evaluateJavascript(
            "if(typeof initializeLocalizedPage==='function')initializeLocalizedPage();",
            null
        )
    }

    override fun onStop() {
        trustedPage = false
        wizardSpeech.close()
        webView.removeJavascriptInterface("BleLockWizardSpeech")
        webView.removeJavascriptInterface("BleLockFirmwareTransfer")
        webView.stopLoading()
        webView.destroy()
        super.onStop()
    }

    private fun textResponse(status: Int, reason: String, message: String) =
        WebResourceResponse("text/plain", "UTF-8", status, reason,
            mapOf("Cache-Control" to "no-store"), ByteArrayInputStream(message.toByteArray(Charsets.UTF_8)))
}
