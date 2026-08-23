package com.blelock.app

import android.content.Context
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.Matrix
import android.graphics.RectF
import android.graphics.drawable.ColorDrawable
import android.graphics.drawable.RippleDrawable
import android.util.AttributeSet
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.FrameLayout
import android.widget.ImageView
import kotlin.math.max
import kotlin.math.roundToInt

/**
 * Home-screen surface that renders the approved background with the exact same
 * center-crop transform used to position the three character hit targets.
 *
 * Hit regions are defined in normalized source-image coordinates, so they stay
 * attached to the subjects on different phone aspect ratios. The image itself
 * is never stretched independently on X/Y.
 */
class CharacterControlsLayout @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : FrameLayout(context, attrs) {

    val automaticButton: Button = characterButton("Автоматический режим")
    val alwaysOpenButton: Button = characterButton("Замок постоянно открыт")
    val settingsButton: Button = characterButton("Настройки")

    private val imageView = ImageView(context).apply {
        scaleType = ImageView.ScaleType.MATRIX
        importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
        contentDescription = null
    }

    private val imageMatrixValue = Matrix()

    // Regions measured against the approved 1080 x 2400 composition.
    // They intentionally do not overlap.
    private val automaticRegion = RectF(0.045f, 0.010f, 0.500f, 0.455f)
    private val alwaysOpenRegion = RectF(0.575f, 0.055f, 0.805f, 0.405f)
    private val settingsRegion = RectF(0.805f, 0.055f, 0.995f, 0.405f)

    init {
        setBackgroundColor(Color.BLACK)
        addView(imageView, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT))
        addView(automaticButton)
        addView(alwaysOpenButton)
        addView(settingsButton)

        val imageId = resources.getIdentifier("home_background", "drawable", context.packageName)
        if (imageId != 0) imageView.setImageResource(imageId)
    }

    fun setAutomaticActive(active: Boolean) = setActive(automaticButton, active)
    fun setAlwaysOpenActive(active: Boolean) = setActive(alwaysOpenButton, active)

    fun isCharacterButton(button: Button): Boolean =
        button === automaticButton || button === alwaysOpenButton || button === settingsButton

    fun setButtonActive(button: Button, active: Boolean) {
        if (!isCharacterButton(button)) return
        setActive(button, active)
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        updateTransformAndTargets(w, h)
    }

    private fun updateTransformAndTargets(width: Int, height: Int) {
        if (width <= 0 || height <= 0) return

        val drawable = imageView.drawable
        val sourceWidth = (drawable?.intrinsicWidth ?: SOURCE_WIDTH).coerceAtLeast(1)
        val sourceHeight = (drawable?.intrinsicHeight ?: SOURCE_HEIGHT).coerceAtLeast(1)
        val scale = max(
            width.toFloat() / sourceWidth.toFloat(),
            height.toFloat() / sourceHeight.toFloat()
        )
        val dx = (width - sourceWidth * scale) / 2f
        val dy = (height - sourceHeight * scale) / 2f

        imageMatrixValue.reset()
        imageMatrixValue.setScale(scale, scale)
        imageMatrixValue.postTranslate(dx, dy)
        imageView.imageMatrix = imageMatrixValue

        placeButton(automaticButton, automaticRegion, sourceWidth, sourceHeight, scale, dx, dy)
        placeButton(alwaysOpenButton, alwaysOpenRegion, sourceWidth, sourceHeight, scale, dx, dy)
        placeButton(settingsButton, settingsRegion, sourceWidth, sourceHeight, scale, dx, dy)
    }

    private fun placeButton(
        button: Button,
        normalized: RectF,
        sourceWidth: Int,
        sourceHeight: Int,
        scale: Float,
        dx: Float,
        dy: Float
    ) {
        var left = dx + normalized.left * sourceWidth * scale
        var top = dy + normalized.top * sourceHeight * scale
        var right = dx + normalized.right * sourceWidth * scale
        var bottom = dy + normalized.bottom * sourceHeight * scale

        val minTouch = dp(48).toFloat()
        if (right - left < minTouch) {
            val cx = (left + right) / 2f
            left = cx - minTouch / 2f
            right = cx + minTouch / 2f
        }
        if (bottom - top < minTouch) {
            val cy = (top + bottom) / 2f
            top = cy - minTouch / 2f
            bottom = cy + minTouch / 2f
        }

        left = left.coerceIn(0f, width.toFloat())
        top = top.coerceIn(0f, height.toFloat())
        right = right.coerceIn(left + 1f, width.toFloat())
        bottom = bottom.coerceIn(top + 1f, height.toFloat())

        button.layoutParams = LayoutParams(
            (right - left).roundToInt(),
            (bottom - top).roundToInt()
        ).apply {
            leftMargin = left.roundToInt()
            topMargin = top.roundToInt()
        }
    }

    private fun characterButton(label: String): Button = Button(context).apply {
        text = label
        contentDescription = label
        isAllCaps = false
        textSize = 13f
        gravity = Gravity.BOTTOM or Gravity.CENTER_HORIZONTAL
        setTextColor(Color.WHITE)
        setShadowLayer(5f, 0f, 2f, Color.BLACK)
        setPadding(dp(8), dp(8), dp(8), dp(12))
        minWidth = dp(48)
        minHeight = dp(48)
        stateListAnimator = null
        background = RippleDrawable(
            ColorStateList.valueOf(0x33FFFFFF),
            ColorDrawable(Color.TRANSPARENT),
            ColorDrawable(Color.WHITE)
        )
        isFocusable = true
        importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_YES
    }

    private fun setActive(button: Button, active: Boolean) {
        button.isActivated = active
        button.setTextColor(if (active) 0xFF64B5F6.toInt() else Color.WHITE)
        button.setTypeface(button.typeface, if (active) android.graphics.Typeface.BOLD else android.graphics.Typeface.NORMAL)
        button.setShadowLayer(if (active) 7f else 5f, 0f, 2f, Color.BLACK)
        button.invalidate()
    }

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density).roundToInt()

    companion object {
        private const val SOURCE_WIDTH = 1080
        private const val SOURCE_HEIGHT = 2400
    }
}
