package com.blelock.app

import android.animation.ValueAnimator
import android.content.Context
import android.content.res.ColorStateList
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.RippleDrawable
import android.util.AttributeSet
import android.view.Gravity
import android.view.View
import android.view.accessibility.AccessibilityEvent
import android.widget.Button
import androidx.core.view.ViewCompat

class DoorActionButton @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : Button(context, attrs) {
    private val density = resources.displayMetrics.density
    private val blue = context.getColor(R.color.accent_blue)
    private val cyan = 0xFF48B8EA.toInt()
    private val paleBlue = 0xFFEAF7FF.toInt()
    private val dividerColor = 0xFFD7EAF4.toInt()
    private val errorColor = context.getColor(R.color.danger_red)
    private val white = context.getColor(R.color.card_background)
    private var uiState: DoorActionUiState = DoorActionUiState.Idle()
    private var signalAnimator: ValueAnimator? = null
    private var unlockAnimator: ValueAnimator? = null
    private var doorAnimator: ValueAnimator? = null
    private var completionAnimator: ValueAnimator? = null
    private var signalPhase = 0f
    private var unlockProgress = 0f
    private var doorProgress = 0f
    private var completionProgress = 0f
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
        strokeJoin = Paint.Join.ROUND
    }
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }
    private val doorRect = RectF()
    private val lockRect = RectF()

    init {
        text = ""
        isAllCaps = false
        textSize = 1f
        minWidth = 0
        minHeight = 0
        setPadding(0, 0, 0, 0)
        gravity = Gravity.CENTER
        stateListAnimator = null
        elevation = 0f
        isFocusable = true
        importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_YES
        val shape = GradientDrawable().apply {
            shape = GradientDrawable.RECTANGLE
            cornerRadius = 59f * density
            setColor(white)
            setStroke(dp(1.5f).toInt(), 0xFFB8E1F5.toInt())
        }
        background = RippleDrawable(ColorStateList.valueOf(0x223DA9D6), shape, null)
        setUiState(uiState)
    }

    fun setUiState(state: DoorActionUiState) {
        val previous = uiState
        uiState = state
        isEnabled = state is DoorActionUiState.Idle
        val description = DoorActionReducer.accessibilityDescription(state)
        contentDescription = description
        ViewCompat.setStateDescription(this, description)
        sendAccessibilityEvent(AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED)

        val targetUnlock = when (state) {
            DoorActionUiState.Unlocked, DoorActionUiState.DoorOpen -> 1f
            is DoorActionUiState.Idle -> if (state.lastConfirmedLock == ConfirmedLockState.UNLOCKED) 1f else 0f
            is DoorActionUiState.Error -> if (state.lastConfirmedLock == ConfirmedLockState.UNLOCKED) 1f else 0f
            else -> 0f
        }
        if (previous != state) animateUnlock(targetUnlock)
        val targetDoor = if (state == DoorActionUiState.DoorOpen ||
            (state is DoorActionUiState.Idle && state.doorOpen) ||
            (state is DoorActionUiState.Error && state.doorOpen)) 1f else 0f
        if (previous != state) animateDoor(targetDoor)

        if (state is DoorActionUiState.Sending) startSignal() else stopSignal()
        if (state == DoorActionUiState.Completed && previous != state) startCompletion()
        else if (state != DoorActionUiState.Completed) stopCompletion()
        if (state is DoorActionUiState.Error && state.operationFinished && previous != state) {
            val errorState = state
            postDelayed({
                if (uiState == errorState) setUiState(
                    DoorActionUiState.Idle(errorState.lastConfirmedLock, errorState.doorOpen)
                )
            }, 850L)
        }
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val save = canvas.save()
        canvas.scale(density, density)
        val widthDp = width / density
        val heightDp = height / density
        val centerY = heightDp * 0.5f
        val doorCenterX = widthDp * 0.27f
        val lockCenterX = widthDp * 0.73f
        drawDoor(canvas, doorCenterX, centerY)
        stroke.color = dividerColor
        stroke.strokeWidth = dp(1f)
        canvas.drawLine(widthDp * 0.5f, heightDp * 0.27f, widthDp * 0.5f, heightDp * 0.73f, stroke)
        val drawState = uiState
        if (drawState is DoorActionUiState.Sending) {
            if (drawState.brokerAccepted) drawAcceptedSignal(canvas, lockCenterX, centerY)
            else drawSignal(canvas, doorCenterX, lockCenterX, centerY)
        }
        drawLock(canvas, lockCenterX, centerY)
        if (uiState == DoorActionUiState.Completed) drawCompletion(canvas, lockCenterX, centerY)
        if (uiState is DoorActionUiState.Error) drawErrorOutline(canvas, widthDp, heightDp)
        canvas.restoreToCount(save)
    }

    private fun drawDoor(canvas: Canvas, centerX: Float, centerY: Float) {
        val w = 39f
        val h = 58f
        val left = centerX - w / 2f
        val top = centerY - h / 2f
        doorRect.set(left, top, left + w, top + h)
        stroke.color = blue
        stroke.strokeWidth = dp(2.2f)
        stroke.style = Paint.Style.STROKE
        canvas.drawRoundRect(doorRect, dp(1.5f), dp(1.5f), stroke)

        val panel = RectF(left + 3f, top + 3f, left + w - 3f, top + h - 3f)
        val save = canvas.save()
        canvas.rotate(-48f * doorProgress, panel.left, centerY)
        fill.color = if (doorProgress > 0.02f) paleBlue else white
        canvas.drawRect(panel, fill)
        stroke.color = blue
        stroke.strokeWidth = dp(1.8f)
        canvas.drawRect(panel, stroke)
        stroke.style = Paint.Style.FILL
        fill.color = cyan
        canvas.drawCircle(panel.right - 5f, centerY, dp(1.7f), fill)
        canvas.restoreToCount(save)
        if (doorProgress > 0f) {
            stroke.style = Paint.Style.STROKE
            stroke.color = cyan
            stroke.alpha = (95 * doorProgress).toInt()
            stroke.strokeWidth = dp(1.5f)
            val arc = RectF(left - 8f, top + 5f, left + w + 12f, top + h - 5f)
            canvas.drawArc(arc, -68f, 52f * doorProgress, false, stroke)
            stroke.alpha = 255
        }
    }

    private fun drawLock(canvas: Canvas, centerX: Float, centerY: Float) {
        val bodyWidth = 35f
        val bodyHeight = 29f
        val body = RectF(centerX - bodyWidth / 2f, centerY - 8f, centerX + bodyWidth / 2f, centerY + 21f)
        lockRect.set(centerX - 27f, centerY - 34f, centerX + 27f, centerY + 28f)
        if (uiState == DoorActionUiState.Unlocked || uiState == DoorActionUiState.DoorOpen) {
            fill.color = 0x243CB5E5
            canvas.drawCircle(centerX, centerY + 2f, 34f, fill)
        }
        fill.color = paleBlue
        canvas.drawRoundRect(body, dp(5f), dp(5f), fill)
        stroke.style = Paint.Style.STROKE
        stroke.color = blue
        stroke.strokeWidth = dp(2.2f)
        canvas.drawRoundRect(body, dp(5f), dp(5f), stroke)

        val shackle = Path().apply {
            if (unlockProgress < 0.5f) {
                moveTo(centerX - 11f, centerY - 8f)
                lineTo(centerX - 11f, centerY - 19f)
                quadTo(centerX - 11f, centerY - 35f, centerX, centerY - 35f)
                quadTo(centerX + 11f, centerY - 35f, centerX + 11f, centerY - 19f)
                lineTo(centerX + 11f, centerY - 8f)
            } else {
                val p = ((unlockProgress - 0.5f) * 2f).coerceIn(0f, 1f)
                moveTo(centerX - 11f, centerY - 8f)
                lineTo(centerX - 11f, centerY - 19f)
                quadTo(centerX - 11f, centerY - 35f, centerX, centerY - 35f)
                quadTo(centerX + 11f, centerY - 35f, centerX + 11f, centerY - 19f)
                lineTo(centerX + 11f + 7f * p, centerY - 13f - 8f * p)
            }
        }
        stroke.style = Paint.Style.STROKE
        stroke.color = blue
        stroke.strokeWidth = dp(3f)
        canvas.drawPath(shackle, stroke)
        stroke.style = Paint.Style.FILL
        fill.color = blue
        canvas.drawCircle(centerX, centerY + 2f, dp(2f), fill)
        canvas.drawRoundRect(RectF(centerX - dp(1f), centerY + 3f, centerX + dp(1f), centerY + 10f), dp(1f), dp(1f), fill)
    }

    private fun drawSignal(canvas: Canvas, doorX: Float, lockX: Float, centerY: Float) {
        val start = doorX + 29f
        val end = lockX - 30f
        val x = start + (end - start) * signalPhase
        for (i in 2 downTo 0) {
            val tailX = x - i * 8f
            if (tailX < start - 1f) continue
            fill.color = cyan
            fill.alpha = 65 + (170 * (1f - i / 3f)).toInt()
            canvas.drawCircle(tailX, centerY, dp(if (i == 0) 3.2f else 2.1f - i * 0.2f), fill)
        }
        fill.alpha = 255
        stroke.color = cyan
        stroke.alpha = 110
        stroke.strokeWidth = dp(5f)
        fill.color = cyan
        fill.alpha = 45
        canvas.drawCircle(x, centerY, dp(6f), fill)
        fill.alpha = 255
        stroke.alpha = 255
    }

    private fun drawAcceptedSignal(canvas: Canvas, centerX: Float, centerY: Float) {
        fill.color = cyan
        fill.alpha = 22
        canvas.drawCircle(centerX, centerY - 2f, 32f, fill)
        fill.alpha = 255
        stroke.style = Paint.Style.STROKE
        stroke.color = cyan
        stroke.alpha = 175
        stroke.strokeWidth = dp(1.8f)
        canvas.drawArc(RectF(centerX - 26f, centerY - 32f, centerX + 26f, centerY + 28f),
            -115f, 245f, false, stroke)
        stroke.alpha = 255
    }

    private fun drawCompletion(canvas: Canvas, centerX: Float, centerY: Float) {
        stroke.style = Paint.Style.STROKE
        stroke.color = cyan
        stroke.alpha = 220
        stroke.strokeWidth = dp(2.4f)
        canvas.drawArc(lockRect, -90f, 360f * completionProgress, false, stroke)
        stroke.alpha = 255
        if (completionProgress >= 0.72f) {
            val check = Path().apply {
                moveTo(centerX + 18f, centerY - 23f)
                lineTo(centerX + 22f, centerY - 19f)
                lineTo(centerX + 30f, centerY - 29f)
            }
            stroke.color = cyan
            stroke.strokeWidth = dp(2.5f)
            canvas.drawPath(check, stroke)
        }
    }

    private fun drawErrorOutline(canvas: Canvas, widthDp: Float, heightDp: Float) {
        stroke.style = Paint.Style.STROKE
        stroke.color = errorColor
        stroke.alpha = 180
        stroke.strokeWidth = dp(2f)
        canvas.drawRoundRect(RectF(1f, 1f, widthDp - 1f, heightDp - 1f), heightDp / 2f, heightDp / 2f, stroke)
        stroke.alpha = 255
    }

    private fun animateUnlock(target: Float) {
        unlockAnimator?.cancel()
        unlockAnimator = ValueAnimator.ofFloat(unlockProgress, target).apply {
            duration = 240L
            addUpdateListener { unlockProgress = it.animatedValue as Float; invalidate() }
            start()
        }
    }

    private fun animateDoor(target: Float) {
        doorAnimator?.cancel()
        doorAnimator = ValueAnimator.ofFloat(doorProgress, target).apply {
            duration = 300L
            addUpdateListener { doorProgress = it.animatedValue as Float; invalidate() }
            start()
        }
    }

    private fun startSignal() {
        if (signalAnimator?.isRunning == true) return
        signalAnimator = ValueAnimator.ofFloat(0f, 1f).apply {
            duration = 1050L
            repeatCount = ValueAnimator.INFINITE
            addUpdateListener { signalPhase = it.animatedValue as Float; invalidate() }
            start()
        }
    }

    private fun stopSignal() {
        signalAnimator?.cancel()
        signalAnimator = null
        signalPhase = 0f
    }

    private fun startCompletion() {
        completionAnimator?.cancel()
        completionProgress = 0f
        completionAnimator = ValueAnimator.ofFloat(0f, 1f).apply {
            duration = 450L
            addUpdateListener { completionProgress = it.animatedValue as Float; invalidate() }
            start()
        }
        postDelayed({
            if (uiState == DoorActionUiState.Completed) setUiState(DoorActionUiState.Idle())
        }, 650L)
    }

    private fun stopCompletion() {
        completionAnimator?.cancel()
        completionAnimator = null
        completionProgress = 0f
    }

    private fun dp(value: Float): Float = value * density
}
