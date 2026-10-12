package com.blelock.app

import android.content.Context
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.text.InputType
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.*
import kotlin.math.max

internal object SettingsUi {
    const val PAGE_BG = 0xFFF2F2F7.toInt()
    const val ROW_BG = Color.WHITE
    const val DIVIDER = 0xFFD1D1D6.toInt()
    const val SECONDARY = 0xFF77777D.toInt()
    const val ACCENT = 0xFF007AFF.toInt()

    fun Context.dp(value: Int): Int = (value * resources.displayMetrics.density).toInt()

    fun matchWrap() = LinearLayout.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.WRAP_CONTENT
    )

    fun sectionHeader(context: Context, title: String): TextView = TextView(context).apply {
        text = title.uppercase()
        textSize = 13f
        setTextColor(SECONDARY)
        setPadding(context.dp(16), context.dp(22), context.dp(16), context.dp(7))
        setBackgroundColor(PAGE_BG)
    }

    fun divider(context: Context): View = View(context).apply {
        setBackgroundColor(DIVIDER)
        layoutParams = LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            max(1, context.dp(1))
        ).apply { marginStart = context.dp(16) }
    }

    fun topBar(context: Context, title: String): LinearLayout = LinearLayout(context).apply {
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
        setPadding(context.dp(16), context.dp(8), context.dp(16), context.dp(8))
        setBackgroundColor(ROW_BG)
        minimumHeight = context.dp(56)
        addView(Space(context), LinearLayout.LayoutParams(context.dp(54), 1))
        addView(TextView(context).apply {
            text = title
            textSize = 18f
            setTextColor(Color.BLACK)
            gravity = Gravity.CENTER
            setTypeface(typeface, Typeface.BOLD)
        }, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        addView(TextView(context).apply {
            text = context.getString(R.string.done)
            textSize = 17f
            gravity = Gravity.END or Gravity.CENTER_VERTICAL
            setTextColor(ACCENT)
            setPadding(0, context.dp(10), 0, context.dp(10))
        }, LinearLayout.LayoutParams(context.dp(54), ViewGroup.LayoutParams.WRAP_CONTENT))
    }
}

internal class NumberSettingRow(
    context: Context,
    title: String,
    subtitle: String? = null,
    signed: Boolean = false,
    private val step: Int = 1
) : LinearLayout(context) {
    val editor: EditText

    init {
        orientation = HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
        setPadding(context.dp(16), context.dp(8), context.dp(16), context.dp(8))
        setBackgroundColor(SettingsUi.ROW_BG)
        minimumHeight = context.dp(62)

        val labels = LinearLayout(context).apply {
            orientation = VERTICAL
            gravity = Gravity.CENTER_VERTICAL
            addView(TextView(context).apply {
                text = title
                textSize = 17f
                setTextColor(Color.BLACK)
            })
            if (!subtitle.isNullOrBlank()) addView(TextView(context).apply {
                text = subtitle
                textSize = 13f
                setTextColor(SettingsUi.SECONDARY)
            })
        }
        addView(labels, LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))

        val control = LinearLayout(context).apply {
            orientation = HORIZONTAL
            gravity = Gravity.CENTER
            background = rounded(context, 0xFFF0F0F3.toInt(), 9)
        }
        val minus = compactButton(context, "−")
        editor = EditText(context).apply {
            isSingleLine = true
            gravity = Gravity.CENTER
            textSize = 16f
            setTextColor(SettingsUi.SECONDARY)
            setBackgroundColor(Color.TRANSPARENT)
            setPadding(context.dp(4), 0, context.dp(4), 0)
            inputType = InputType.TYPE_CLASS_NUMBER or
                if (signed) InputType.TYPE_NUMBER_FLAG_SIGNED else 0
            setSelectAllOnFocus(true)
        }
        val plus = compactButton(context, "+")
        minus.setHapticClickListener { nudge(-step) }
        plus.setHapticClickListener { nudge(step) }
        control.addView(minus, LayoutParams(context.dp(42), context.dp(38)))
        control.addView(editor, LayoutParams(context.dp(76), context.dp(38)))
        control.addView(plus, LayoutParams(context.dp(42), context.dp(38)))
        addView(control)
    }

    private fun nudge(delta: Int) {
        val current = editor.text.toString().toIntOrNull() ?: 0
        editor.setText((current + delta).toString())
        editor.setSelection(editor.text.length)
    }

    private fun compactButton(context: Context, label: String) = TextView(context).apply {
        text = label
        textSize = 25f
        gravity = Gravity.CENTER
        setTextColor(Color.BLACK)
        isClickable = true
        isFocusable = true
        background = context.obtainStyledAttributes(intArrayOf(android.R.attr.selectableItemBackground)).getDrawable(0)
    }

    private fun rounded(context: Context, color: Int, radiusDp: Int) = GradientDrawable().apply {
        setColor(color)
        cornerRadius = context.dp(radiusDp).toFloat()
    }
}

internal class SwitchSettingRow(
    context: Context,
    title: String,
    subtitle: String? = null
) : LinearLayout(context) {
    val switch: Switch

    init {
        orientation = HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
        setPadding(context.dp(16), context.dp(7), context.dp(10), context.dp(7))
        setBackgroundColor(SettingsUi.ROW_BG)
        minimumHeight = context.dp(58)
        val labels = LinearLayout(context).apply {
            orientation = VERTICAL
            addView(TextView(context).apply {
                text = title
                textSize = 17f
                setTextColor(Color.BLACK)
            })
            if (!subtitle.isNullOrBlank()) addView(TextView(context).apply {
                text = subtitle
                textSize = 13f
                setTextColor(SettingsUi.SECONDARY)
            })
        }
        addView(labels, LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        switch = Switch(context).apply { text = "" }
        addView(switch)
    }
}

internal class SpinnerSettingRow(
    context: Context,
    title: String,
    labels: Array<String>
) : LinearLayout(context) {
    val spinner: Spinner

    init {
        orientation = HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
        setPadding(context.dp(16), context.dp(7), context.dp(8), context.dp(7))
        setBackgroundColor(SettingsUi.ROW_BG)
        minimumHeight = context.dp(58)
        addView(TextView(context).apply {
            text = title
            textSize = 17f
            setTextColor(Color.BLACK)
        }, LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        spinner = Spinner(context).apply {
            adapter = ArrayAdapter(context, android.R.layout.simple_spinner_dropdown_item, labels)
        }
        addView(spinner, LayoutParams(context.dp(145), ViewGroup.LayoutParams.WRAP_CONTENT))
    }
}

internal fun Context.dp(value: Int): Int = (value * resources.displayMetrics.density).toInt()
