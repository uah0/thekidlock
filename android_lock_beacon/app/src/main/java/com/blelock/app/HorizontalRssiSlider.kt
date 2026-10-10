package com.blelock.app

import android.content.Context
import android.annotation.SuppressLint
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.view.MotionEvent
import android.view.View
import android.view.accessibility.AccessibilityEvent
import android.view.accessibility.AccessibilityNodeInfo
import android.widget.SeekBar
import kotlin.math.roundToInt

/** Горизонтальный дискретный ползунок с доступностью обычного SeekBar. */
@SuppressLint("ViewConstructor")
internal class HorizontalLevelSlider(
    context: Context,
    private val minValue: Int,
    private val maxValue: Int,
    private val valueDescription: String = "Порог RSSI маяка"
) : View(context) {
    private val density = resources.displayMetrics.density
    private val trackPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = SettingsUi.DIVIDER
    }
    private val activePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = SettingsUi.ACCENT
    }
    private val thumbPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = SettingsUi.ACCENT
        style = Paint.Style.FILL
    }
    private val thumbOutlinePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = SettingsUi.ROW_BG
        style = Paint.Style.STROKE
        strokeWidth = 3f * density
    }

    var onValueChanged: ((Int) -> Unit)? = null
    var value: Int = minValue
        private set
    private var dragging = false
    private val trackPath = Path()
    private val minX get() = paddingLeft + 18f * density
    private val maxX get() = width - paddingRight - 18f * density

    init {
        isFocusable = true
        isClickable = true
        importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_YES
        contentDescription = valueDescription
    }

    fun setValue(newValue: Int, notify: Boolean = true) {
        val bounded = newValue.coerceIn(minValue, maxValue)
        if (bounded == value) return
        value = bounded
        invalidate()
        if (notify) {
            onValueChanged?.invoke(value)
            sendAccessibilityEvent(AccessibilityEvent.TYPE_VIEW_SELECTED)
        }
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val centerY = height / 2f
        val left = minX
        val right = maxX.coerceAtLeast(left + 1f)
        val fraction = (value - minValue).toFloat() / (maxValue - minValue)
        val thumbX = left + fraction * (right - left)
        val thinRadius = 2f * density
        val thickRadius = 7f * density
        trackPath.reset()
        trackPath.moveTo(left, centerY - thinRadius)
        trackPath.lineTo(right, centerY - thickRadius)
        trackPath.cubicTo(right + thickRadius, centerY - thickRadius,
            right + thickRadius, centerY + thickRadius, right, centerY + thickRadius)
        trackPath.lineTo(left, centerY + thinRadius)
        trackPath.cubicTo(left - thinRadius, centerY + thinRadius,
            left - thinRadius, centerY - thinRadius, left, centerY - thinRadius)
        trackPath.close()
        canvas.drawPath(trackPath, trackPaint)
        val savedCanvas = canvas.save()
        canvas.clipRect(left - thinRadius, 0f, thumbX, height.toFloat())
        canvas.drawPath(trackPath, activePaint)
        canvas.restoreToCount(savedCanvas)
        canvas.drawCircle(thumbX, centerY, 12f * density, thumbPaint)
        canvas.drawCircle(thumbX, centerY, 12f * density, thumbOutlinePaint)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                parent?.requestDisallowInterceptTouchEvent(true)
                dragging = true
                isPressed = true
                updateFromX(event.x)
                return true
            }
            MotionEvent.ACTION_MOVE -> {
                if (!dragging) return false
                updateFromX(event.x)
                return true
            }
            MotionEvent.ACTION_UP -> {
                if (!dragging) return false
                updateFromX(event.x)
                dragging = false
                isPressed = false
                parent?.requestDisallowInterceptTouchEvent(false)
                performClick()
                return true
            }
            MotionEvent.ACTION_CANCEL -> {
                dragging = false
                isPressed = false
                parent?.requestDisallowInterceptTouchEvent(false)
                return true
            }
        }
        return true
    }

    private fun updateFromX(x: Float) {
        val left = minX
        val right = maxX.coerceAtLeast(left + 1f)
        val fraction = ((x - left) / (right - left)).coerceIn(0f, 1f)
        setValue(minValue + (fraction * (maxValue - minValue)).roundToInt())
    }

    override fun performClick(): Boolean {
        super.performClick()
        return true
    }

    override fun onInitializeAccessibilityNodeInfo(info: AccessibilityNodeInfo) {
        super.onInitializeAccessibilityNodeInfo(info)
        info.className = SeekBar::class.java.name
        info.rangeInfo = AccessibilityNodeInfo.RangeInfo.obtain(
            AccessibilityNodeInfo.RangeInfo.RANGE_TYPE_INT,
            minValue.toFloat(),
            maxValue.toFloat(),
            value.toFloat()
        )
        info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_FORWARD)
        info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_BACKWARD)
        info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SET_PROGRESS)
    }

    override fun performAccessibilityAction(action: Int, arguments: android.os.Bundle?): Boolean {
        when (action) {
            AccessibilityNodeInfo.ACTION_SCROLL_FORWARD -> setValue(value + 1)
            AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD -> setValue(value - 1)
            AccessibilityNodeInfo.AccessibilityAction.ACTION_SET_PROGRESS.id -> {
                val requested = arguments?.getFloat(
                    AccessibilityNodeInfo.ACTION_ARGUMENT_PROGRESS_VALUE
                ) ?: return false
                setValue(requested.roundToInt())
            }
            else -> return super.performAccessibilityAction(action, arguments)
        }
        return true
    }
}
