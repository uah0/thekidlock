package com.blelock.app

import android.app.AlertDialog
import android.content.DialogInterface
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View

internal fun View.buttonHaptic() {
    performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
}

internal fun View.setHapticClickListener(action: (View) -> Unit) {
    setOnClickListener { view ->
        view.buttonHaptic()
        action(view)
    }
}

internal fun AlertDialog.showWithHapticButtons(): AlertDialog = apply {
    show()
    listOf(DialogInterface.BUTTON_POSITIVE, DialogInterface.BUTTON_NEGATIVE,
        DialogInterface.BUTTON_NEUTRAL).forEach { which ->
        getButton(which)?.setOnTouchListener { view, event ->
            if (event.actionMasked == MotionEvent.ACTION_UP && view.isPressed &&
                event.x >= 0 && event.x < view.width && event.y >= 0 && event.y < view.height) {
                view.buttonHaptic()
            }
            false
        }
    }
}
