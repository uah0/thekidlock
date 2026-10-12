package com.blelock.app

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.util.AttributeSet
import android.widget.Button

/** Button with four right-arrow steps while the phone opens the lock Web UI. */
class WebUiProgressButton @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : Button(context, attrs) {
    private val density = resources.displayMetrics.density
    private val arrowPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val arrowPath = Path()
    private var activeStep = -1
    private var fullHeight = minimumHeight
    private var fullPadding = intArrayOf(paddingLeft, paddingTop, paddingRight, paddingBottom)
    private val activeColor = context.getColor(R.color.accent_blue)
    private val inactiveColor = 0xFFE3E8EF.toInt()

    fun showProgress(step: Int, complete: Boolean = false) {
        if (step !in 0..3) return
        if (activeStep < 0) {
            fullHeight = minimumHeight
            fullPadding = intArrayOf(paddingLeft, paddingTop, paddingRight, paddingBottom)
        }
        activeStep = if (complete) 3 else step
        invalidate()
    }

    fun clearProgress() {
        activeStep = -1
        minHeight = fullHeight
        setPadding(fullPadding[0], fullPadding[1], fullPadding[2], fullPadding[3])
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        if (activeStep < 0) return

        val left = dp(18).toFloat()
        val right = width - dp(18).toFloat()
        val top = (height - dp(21)).toFloat()
        val bottom = (height - dp(7)).toFloat()
        val gap = dp(3).toFloat()
        val segmentWidth = (right - left) / 4f
        val head = dp(12).toFloat()
        val notch = dp(7).toFloat()

        for (index in 0 until 4) {
            val x0 = left + index * segmentWidth + gap / 2f
            val x1 = left + (index + 1) * segmentWidth - gap / 2f
            val mid = (top + bottom) / 2f
            arrowPath.reset()
            arrowPath.moveTo(if (index == 0) x0 else x0 + notch, top)
            arrowPath.lineTo(x1 - head, top)
            arrowPath.lineTo(x1, mid)
            arrowPath.lineTo(x1 - head, bottom)
            arrowPath.lineTo(if (index == 0) x0 else x0 + notch, bottom)
            if (index > 0) arrowPath.lineTo(x0 + head, mid)
            arrowPath.close()
            arrowPaint.color = if (index <= activeStep) activeColor else inactiveColor
            canvas.drawPath(arrowPath, arrowPaint)
        }
    }

    private fun dp(value: Int): Int = (value * density).toInt()
}
