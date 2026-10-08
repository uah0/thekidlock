package com.blelock.app

import android.animation.Animator
import android.animation.ValueAnimator
import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.RippleDrawable
import android.content.res.ColorStateList
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.AttributeSet
import android.view.View

enum class InternetOpenStage {
    IDLE, CONNECTING, COMMAND_SENT, UNLOCKED, CLOSING, CLOSED, CLOSED_WITHOUT_DOOR_OPEN, ERROR
}

internal fun internetOpenStageForReply(
    result: String, faults: Long, door: String? = null,
    lockWasOpened: Boolean = false, doorWasOpened: Boolean = false
): InternetOpenStage = when {
    result == "opened" && faults == 0L -> InternetOpenStage.UNLOCKED
    result == "closing" && door == "closed" && lockWasOpened && !doorWasOpened && faults == 0L ->
        InternetOpenStage.UNLOCKED
    result == "closing" -> InternetOpenStage.CLOSING
    result == "closed" && door == "closed" && lockWasOpened && !doorWasOpened && faults == 0L ->
        InternetOpenStage.CLOSED_WITHOUT_DOOR_OPEN
    result == "closed" && door != "open" && faults == 0L -> InternetOpenStage.CLOSED
    result in listOf("opened", "closed", "error", "interrupted") -> InternetOpenStage.ERROR
    else -> InternetOpenStage.ERROR
}

class InternetOpenProgressButton @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {
    private val density = resources.displayMetrics.density
    private val handler = Handler(Looper.getMainLooper())
    private val rect = RectF()
    private val progressRect = RectF()
    private val outerPath = Path()
    private val fillPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val borderPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = dp(1.5f)
        color = context.getColor(R.color.accent_blue)
    }
    private val dividerPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        strokeWidth = dp(1f)
        color = context.getColor(R.color.card_background)
    }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = context.getColor(R.color.accent_blue)
        textSize = 18f * resources.displayMetrics.scaledDensity
        typeface = Typeface.create(Typeface.DEFAULT, Typeface.BOLD)
        textAlign = Paint.Align.CENTER
    }
    private val colors = intArrayOf(
        context.getColor(R.color.secondary_text),
        0xFFFFC107.toInt(),
        context.getColor(R.color.switch_track_on),
        0xFFFFC107.toInt(),
        context.getColor(R.color.danger_red)
    )
    private val idleColor = context.getColor(R.color.card_background)
    private val inactiveColor = context.getColor(R.color.stepper_pill_background)
    private val idleLabel = context.getString(R.string.open_via_internet)
    private var stage = InternetOpenStage.IDLE
    private var blinkOn = false
    private var blinkAnimator: ValueAnimator? = null

    init {
        isClickable = true
        isFocusable = true
        elevation = dp(5f)
        importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_YES
        contentDescription = idleLabel
        foreground = RippleDrawable(
            ColorStateList.valueOf(0x331565C0),
            null,
            GradientDrawable().apply {
                shape = GradientDrawable.RECTANGLE
                cornerRadius = dp(14f)
            }
        )
        minimumHeight = dp(96f).toInt()
        minimumWidth = dp(200f).toInt()
    }

    fun showStage(next: InternetOpenStage, doorState: String? = null) {
        handler.removeCallbacksAndMessages(null)
        blinkAnimator?.cancel()
        blinkAnimator = null
        blinkOn = false
        stage = next
        contentDescription = descriptionFor(next, doorState)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) stateDescription = contentDescription

        when (next) {
            InternetOpenStage.CLOSED, InternetOpenStage.CLOSED_WITHOUT_DOOR_OPEN -> {
                isEnabled = false
                scheduleReset(1000L)
            }
            InternetOpenStage.ERROR -> startErrorBlink()
            InternetOpenStage.IDLE -> isEnabled = true
            else -> isEnabled = false
        }
        invalidate()
    }

    private fun startErrorBlink() {
        isEnabled = false
        blinkAnimator = ValueAnimator.ofFloat(0f, 1f).apply {
            duration = 130L
            repeatMode = ValueAnimator.REVERSE
            repeatCount = 5
            addUpdateListener {
                blinkOn = it.animatedValue as Float >= 0.5f
                invalidate()
            }
            addListener(object : Animator.AnimatorListener {
                override fun onAnimationStart(animation: Animator) = Unit
                override fun onAnimationCancel(animation: Animator) = Unit
                override fun onAnimationRepeat(animation: Animator) = Unit
                override fun onAnimationEnd(animation: Animator) { invalidate() }
            })
            start()
        }
        scheduleReset(1000L)
    }

    private fun scheduleReset(delayMs: Long) {
        val task = Runnable {
            stage = InternetOpenStage.IDLE
            blinkOn = false
            blinkAnimator?.cancel()
            blinkAnimator = null
            contentDescription = idleLabel
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) stateDescription = idleLabel
            isEnabled = true
            invalidate()
        }
        handler.postDelayed(task, delayMs)
    }

    private fun descriptionFor(value: InternetOpenStage, doorState: String?): String {
        val base = when (value) {
            InternetOpenStage.IDLE -> idleLabel
            InternetOpenStage.CONNECTING -> "Подключение к брокеру"
            InternetOpenStage.COMMAND_SENT -> "Команда отправлена, ожидание замка"
            InternetOpenStage.UNLOCKED -> "Замок открыт"
            InternetOpenStage.CLOSING -> "Замок закрывается, ожидание ответной пластины"
            InternetOpenStage.CLOSED -> "Замок закрыт"
            InternetOpenStage.CLOSED_WITHOUT_DOOR_OPEN -> "Замок закрылся, дверь не открывалась"
            InternetOpenStage.ERROR -> "Ошибка интернет-открытия"
        }
        return when (doorState) {
            "open" -> "$base. Дверь открыта"
            "closed" -> "$base. Дверь закрыта"
            else -> base
        }
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val inset = dp(1f)
        rect.set(inset, inset, width - inset, height - inset)
        val radius = dp(14f)
        outerPath.reset()
        outerPath.addRoundRect(rect, radius, radius, Path.Direction.CW)

        val splitY = rect.centerY()
        progressRect.set(rect.left, splitY, rect.right, rect.bottom)
        canvas.save()
        canvas.clipPath(outerPath)
        fillPaint.color = idleColor
        canvas.drawRect(rect, fillPaint)
        fillPaint.color = inactiveColor
        canvas.drawRect(progressRect, fillPaint)

        if (stage == InternetOpenStage.ERROR) {
            if (blinkOn) {
                fillPaint.color = colors.last()
                canvas.drawRect(progressRect, fillPaint)
            }
        } else {
            val activeCount = when (stage) {
                InternetOpenStage.CONNECTING -> 1
                InternetOpenStage.COMMAND_SENT -> 2
                InternetOpenStage.UNLOCKED -> 3
                InternetOpenStage.CLOSING -> 4
                InternetOpenStage.CLOSED -> 5
                InternetOpenStage.CLOSED_WITHOUT_DOOR_OPEN -> 4
                else -> 0
            }
            val segmentWidth = rect.width() / 5f
            for (index in 0 until activeCount) {
                fillPaint.color = if (stage == InternetOpenStage.CLOSED_WITHOUT_DOOR_OPEN && index == 3) {
                    colors.last()
                } else colors[index]
                val left = rect.left + index * segmentWidth
                canvas.drawRect(left, splitY, left + segmentWidth, rect.bottom, fillPaint)
            }
        }

        val labelBaseline = rect.top + rect.height() / 4f - (textPaint.ascent() + textPaint.descent()) / 2f
        canvas.drawText(idleLabel, width / 2f, labelBaseline, textPaint)

        val segmentWidth = rect.width() / 5f
        for (index in 1..4) {
            val x = rect.left + index * segmentWidth
            canvas.drawLine(x, splitY + dp(4f), x, rect.bottom - dp(4f), dividerPaint)
        }
        canvas.drawLine(rect.left + dp(8f), splitY, rect.right - dp(8f), splitY, dividerPaint)
        canvas.restore()

        canvas.drawRoundRect(rect, radius, radius, borderPaint)
    }

    override fun onDetachedFromWindow() {
        handler.removeCallbacksAndMessages(null)
        blinkAnimator?.cancel()
        super.onDetachedFromWindow()
    }

    private fun dp(value: Float): Float = value * density
}
