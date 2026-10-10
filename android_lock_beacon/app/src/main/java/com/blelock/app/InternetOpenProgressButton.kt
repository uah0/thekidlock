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
    IDLE, CONNECTING, COMMAND_SENT, BROKER_ACCEPTED, UNLOCKED, DOOR_OPEN, CLOSING, CLOSED, CLOSED_WITHOUT_DOOR_OPEN, ERROR
}

internal fun internetOpenDisplayedStage(previous: InternetOpenStage, next: InternetOpenStage): InternetOpenStage =
    if (next == InternetOpenStage.CLOSING || next == InternetOpenStage.ERROR) previous else next

internal fun internetOpenStageForReply(
    result: String, faults: Long, door: String? = null,
    lockWasOpened: Boolean = false, doorWasOpened: Boolean = false
): InternetOpenStage = when {
    faults != 0L -> InternetOpenStage.ERROR
    result in listOf("opened", "closing") && door == "open" -> InternetOpenStage.DOOR_OPEN
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
    private var displayedStage = InternetOpenStage.IDLE
    private var blinkOn = false
    private var blinkAnimator: ValueAnimator? = null

    init {
        isClickable = true
        isFocusable = true
        elevation = dp(1f)
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
        displayedStage = internetOpenDisplayedStage(displayedStage, next)
        contentDescription = descriptionFor(next, doorState)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) stateDescription = contentDescription

        when (next) {
            InternetOpenStage.CLOSED, InternetOpenStage.CLOSED_WITHOUT_DOOR_OPEN -> {
                isEnabled = false
                scheduleReset(3000L)
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
            displayedStage = InternetOpenStage.IDLE
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
        return context.getString(when (value) {
            InternetOpenStage.IDLE -> R.string.open_via_internet
            InternetOpenStage.CONNECTING -> R.string.internet_command_sent
            InternetOpenStage.COMMAND_SENT -> R.string.internet_command_sent
            InternetOpenStage.BROKER_ACCEPTED -> R.string.internet_broker_accepted
            InternetOpenStage.UNLOCKED -> R.string.internet_unlocked
            InternetOpenStage.DOOR_OPEN -> R.string.internet_door_opened
            InternetOpenStage.CLOSING -> if (displayedStage == InternetOpenStage.DOOR_OPEN)
                R.string.internet_door_opened else R.string.internet_unlocked
            InternetOpenStage.CLOSED, InternetOpenStage.CLOSED_WITHOUT_DOOR_OPEN ->
                R.string.internet_closed_locked
            InternetOpenStage.ERROR -> R.string.internet_error
        })
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val desiredHeight = (96f * resources.displayMetrics.density *
            resources.configuration.fontScale.coerceAtLeast(1f)).toInt()
        setMeasuredDimension(resolveSize(minimumWidth, widthMeasureSpec),
            resolveSize(desiredHeight, heightMeasureSpec))
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
        if (stage == InternetOpenStage.IDLE) {
            canvas.restore()
            borderPaint.color = 0xFFDCE5F0.toInt()
            canvas.drawRoundRect(rect, radius, radius, borderPaint)
            val icon = context.getDrawable(R.drawable.ic_handoff_cloud)!!
            val iconTop = (height - dp(28f).toInt()) / 2
            icon.setBounds(dp(20f).toInt(), iconTop, dp(48f).toInt(), iconTop + dp(28f).toInt())
            icon.draw(canvas)
            val label = android.text.SpannableString("$idleLabel\n${context.getString(R.string.send_remote_command)}").apply {
                setSpan(android.text.style.RelativeSizeSpan(0.72f), idleLabel.length + 1, length, 0)
                setSpan(android.text.style.ForegroundColorSpan(context.getColor(R.color.secondary_text)), idleLabel.length + 1, length, 0)
            }
            val paint = android.text.TextPaint(textPaint).apply {
                color = context.getColor(R.color.primary_text)
                textAlign = Paint.Align.LEFT
            }
            val layout = android.text.StaticLayout.Builder.obtain(label, 0, label.length, paint,
                (width - dp(88f).toInt()).coerceAtLeast(1)).build()
            canvas.save()
            canvas.translate(dp(64f), (height - layout.height) / 2f)
            layout.draw(canvas)
            canvas.restore()
            return
        }
        borderPaint.color = context.getColor(R.color.accent_blue)
        fillPaint.color = inactiveColor
        canvas.drawRect(progressRect, fillPaint)

        if (stage == InternetOpenStage.ERROR) {
            if (blinkOn) {
                fillPaint.color = colors.last()
                canvas.drawRect(progressRect, fillPaint)
            }
        } else {
            val activeCount = when (displayedStage) {
                InternetOpenStage.CONNECTING, InternetOpenStage.COMMAND_SENT -> 1
                InternetOpenStage.BROKER_ACCEPTED -> 2
                InternetOpenStage.UNLOCKED -> 3
                InternetOpenStage.DOOR_OPEN -> 4
                InternetOpenStage.CLOSING -> if (displayedStage == InternetOpenStage.DOOR_OPEN) 4 else 3
                InternetOpenStage.CLOSED -> 5
                InternetOpenStage.CLOSED_WITHOUT_DOOR_OPEN -> 5
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

        val statusLabel = descriptionFor(displayedStage, null)
        val statusPaint = android.text.TextPaint(textPaint).apply { textAlign = Paint.Align.LEFT }
        val statusLayout = android.text.StaticLayout.Builder.obtain(statusLabel, 0,
            statusLabel.length, statusPaint, (width - dp(32f).toInt()).coerceAtLeast(1))
            .setAlignment(android.text.Layout.Alignment.ALIGN_CENTER).build()
        canvas.save()
        canvas.translate(dp(16f), rect.top + (splitY - rect.top - statusLayout.height) / 2f)
        statusLayout.draw(canvas)
        canvas.restore()

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
