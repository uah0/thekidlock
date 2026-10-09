package com.blelock.app

import android.app.Activity
import android.app.Instrumentation
import android.content.Intent
import android.os.Bundle
import android.os.Build
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
import android.webkit.WebView
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/** Physical-device test against a loopback fixture, never an actual OTA endpoint. */
class WebUiOtaInstrumentation : Instrumentation() {
    override fun onCreate(arguments: Bundle?) {
        super.onCreate(arguments)
        start()
    }

    override fun onStart() {
        var dialog: LockWebUiDialog? = null
        try {
            val activity = startActivitySync(Intent(targetContext, MainActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            lateinit var webView: WebView
            runOnMainSync {
                dialog = LockWebUiDialog(activity, "http://127.0.0.1/").apply { show() }
                webView = findWebView(dialog!!.window!!.decorView) ?: error("WebView missing")
            }
            awaitCondition(webView, "window.bleLockInternalFirmware===true && " +
                "document.getElementById('uploadFwButton').textContent==='Установить обновление' && " +
                "document.getElementById('fwPicker').style.display==='none'")
            check(evaluate(webView, "document.getElementById('fw').files.length") == "0")
            runOnMainSync {
                val decor = dialog!!.window!!.decorView
                val insets = checkNotNull(decor.rootWindowInsets)
                val topInset = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                    insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.displayCutout()).top
                } else {
                    @Suppress("DEPRECATION")
                    insets.systemWindowInsetTop
                }
                val firstRow = IntArray(2)
                webView.getLocationOnScreen(firstRow)
                check(firstRow[1] >= topInset) { "Web UI header overlaps system bar" }
            }
            evaluate(webView, "document.getElementById('uploadFwButton').click(); true")
            awaitCondition(webView, "document.getElementById('ota').textContent.includes('Замок обновлён')")
            check(evaluate(webView, "document.getElementById('uploadFwButton').disabled") == "false")
            val result = Bundle().apply {
                putString("result", "PASS: system top inset, private image, one button, confirmed OTA completion")
                putString("ui", evaluate(webView, "document.getElementById('ota').textContent"))
            }
            finish(Activity.RESULT_OK, result)
        } catch (error: Throwable) {
            finish(Activity.RESULT_CANCELED, Bundle().apply { putString("error", error.toString()) })
        } finally {
            runOnMainSync { dialog?.dismiss() }
        }
    }

    private fun evaluate(view: WebView, script: String): String {
        val latch = CountDownLatch(1)
        var result = ""
        runOnMainSync { view.evaluateJavascript(script) { result = it; latch.countDown() } }
        check(latch.await(5, TimeUnit.SECONDS)) { "JavaScript callback timeout" }
        return result
    }

    private fun awaitCondition(view: WebView, expression: String) {
        val deadline = System.currentTimeMillis() + 40_000
        while (System.currentTimeMillis() < deadline) {
            if (evaluate(view, expression) == "true") return
            Thread.sleep(250)
        }
        error("Web UI timeout: $expression; URL=${evaluate(view, "location.href")}; " +
            "OTA=${evaluate(view, "document.getElementById('ota')?.textContent")}")
    }

    private fun findWebView(view: View): WebView? {
        if (view is WebView) return view
        if (view is ViewGroup) for (i in 0 until view.childCount) {
            findWebView(view.getChildAt(i))?.let { return it }
        }
        return null
    }
}
