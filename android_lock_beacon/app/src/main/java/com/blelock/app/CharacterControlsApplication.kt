package com.blelock.app

import android.app.Activity
import android.app.Application
import android.graphics.Color
import android.os.Bundle
import android.view.View
import android.view.ViewGroup
import android.view.ViewTreeObserver
import android.widget.Button
import android.widget.FrameLayout
import android.widget.TextView

/**
 * Installs the character-based home controls as a presentation layer over the
 * existing MainActivity. The real existing buttons remain the owners of all
 * BLE/Web UI/OTA behaviour: character taps delegate to their performClick().
 */
class CharacterControlsApplication : Application(), Application.ActivityLifecycleCallbacks {
    override fun onCreate() {
        super.onCreate()
        registerActivityLifecycleCallbacks(this)
    }

    override fun onActivityCreated(activity: Activity, savedInstanceState: Bundle?) {
        if (activity !is MainActivity) return
        activity.window.decorView.post { installHomeOverlay(activity) }
    }

    override fun onActivityResumed(activity: Activity) {
        if (activity is MainActivity) {
            activity.window.decorView.post { refreshOverlay(activity) }
        }
    }

    private fun installHomeOverlay(activity: MainActivity) {
        val content = activity.findViewById<ViewGroup>(android.R.id.content) ?: return
        val appRoot = content.getChildAt(0) as? FrameLayout ?: return
        if (appRoot.findViewWithTag<View>(OVERLAY_TAG) != null) {
            refreshOverlay(activity)
            return
        }

        val autoText = activity.getString(R.string.mode_auto)
        val alwaysText = activity.getString(R.string.mode_always_open)
        val settingsText = activity.getString(R.string.control_settings)
        val privacyText = activity.getString(R.string.privacy_policy_button)
        val permissionText = activity.getString(R.string.grant_permissions)
        val closeText = activity.getString(R.string.admin_close)

        val originalAuto = findButton(appRoot, autoText) ?: return
        val originalAlways = findButton(appRoot, alwaysText) ?: return
        val originalSettings = findButton(appRoot, settingsText) ?: return
        val permissionButton = findButton(appRoot, permissionText)
        val privacyView = findTextView(appRoot, privacyText)
        val adminCloseButton = findButton(appRoot, closeText)

        val overlay = CharacterControlsLayout(activity).apply {
            tag = OVERLAY_TAG
            automaticButton.setOnClickListener { originalAuto.performClick() }
            alwaysOpenButton.setOnClickListener {
                if (originalAlways.isEnabled) originalAlways.performClick()
            }
            settingsButton.setOnClickListener {
                // Programmatic click deliberately delegates to the existing
                // settings/Web UI handler even while its old visual button is hidden.
                originalSettings.performClick()
            }
        }

        // appRoot already applies Android system-bar insets, so a child of this
        // root stays inside the app content area. Leave the existing pinned
        // Privacy Policy footer unobscured.
        appRoot.addView(
            overlay,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            ).apply { bottomMargin = dp(activity, 44) }
        )

        privacyView?.bringToFront()
        permissionButton?.bringToFront()

        val observer = ViewTreeObserver.OnGlobalLayoutListener {
            val permissionVisible = permissionButton?.isShown == true
            val adminVisible = adminCloseButton?.isShown == true
            overlay.visibility = if (permissionVisible || adminVisible) View.GONE else View.VISIBLE
            if (overlay.visibility == View.VISIBLE) {
                overlay.setAutomaticActive(originalAuto.currentTextColor == Color.WHITE)
                overlay.setAlwaysOpenActive(originalAlways.currentTextColor == Color.WHITE)
                overlay.automaticButton.isEnabled = originalAuto.isEnabled
                overlay.alwaysOpenButton.isEnabled = originalAlways.isEnabled
                // Settings remains a valid entry point: its existing handler can
                // establish the administrative BLE session when necessary.
                overlay.settingsButton.isEnabled = originalAuto.isEnabled
            }
            privacyView?.bringToFront()
            permissionButton?.bringToFront()
        }
        appRoot.viewTreeObserver.addOnGlobalLayoutListener(observer)
        appRoot.setTag(OBSERVER_TAG_ID, observer)
        refreshOverlay(activity)
    }

    private fun refreshOverlay(activity: MainActivity) {
        val content = activity.findViewById<ViewGroup>(android.R.id.content) ?: return
        val appRoot = content.getChildAt(0) as? FrameLayout ?: return
        val overlay = appRoot.findViewWithTag<CharacterControlsLayout>(OVERLAY_TAG) ?: return

        val originalAuto = findButton(appRoot, activity.getString(R.string.mode_auto)) ?: return
        val originalAlways = findButton(appRoot, activity.getString(R.string.mode_always_open)) ?: return
        val privacyView = findTextView(appRoot, activity.getString(R.string.privacy_policy_button))
        val permissionVisible = findButton(appRoot, activity.getString(R.string.grant_permissions))?.isShown == true
        val adminVisible = findButton(appRoot, activity.getString(R.string.admin_close))?.isShown == true

        overlay.visibility = if (permissionVisible || adminVisible) View.GONE else View.VISIBLE
        overlay.setAutomaticActive(originalAuto.currentTextColor == Color.WHITE)
        overlay.setAlwaysOpenActive(originalAlways.currentTextColor == Color.WHITE)
        overlay.automaticButton.isEnabled = originalAuto.isEnabled
        overlay.alwaysOpenButton.isEnabled = originalAlways.isEnabled
        overlay.settingsButton.isEnabled = originalAuto.isEnabled
        privacyView?.bringToFront()
    }

    private fun findButton(root: View, text: String): Button? =
        descendants(root).filterIsInstance<Button>().firstOrNull { it.text.toString() == text }

    private fun findTextView(root: View, text: String): TextView? =
        descendants(root).filterIsInstance<TextView>().firstOrNull { it.text.toString() == text }

    private fun descendants(root: View): Sequence<View> = sequence {
        yield(root)
        if (root is ViewGroup) {
            for (index in 0 until root.childCount) {
                yieldAll(descendants(root.getChildAt(index)))
            }
        }
    }

    private fun dp(activity: Activity, value: Int): Int =
        (value * activity.resources.displayMetrics.density).toInt()

    override fun onActivityStarted(activity: Activity) = Unit
    override fun onActivityPaused(activity: Activity) = Unit
    override fun onActivityStopped(activity: Activity) = Unit
    override fun onActivitySaveInstanceState(activity: Activity, outState: Bundle) = Unit
    override fun onActivityDestroyed(activity: Activity) = Unit

    companion object {
        private const val OVERLAY_TAG = "ble-lock-character-controls-overlay"
        private const val OBSERVER_TAG_ID = 0x6B1E0001
    }
}
