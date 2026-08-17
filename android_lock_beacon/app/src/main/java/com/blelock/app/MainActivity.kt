package com.blelock.app

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.content.BroadcastReceiver
import android.content.ComponentName
import android.content.Context
import android.content.IntentFilter
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.Typeface
import android.content.pm.PackageManager
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.net.Uri
import android.provider.Settings
import android.text.Editable
import android.text.InputType
import android.text.TextWatcher
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.AdapterView
import android.widget.Button
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.Spinner
import android.widget.ArrayAdapter
import android.widget.Space
import android.widget.Switch
import android.widget.TextView
import androidx.swiperefreshlayout.widget.SwipeRefreshLayout

class MainActivity : Activity() {
    private lateinit var autoOpenSwitch: Switch
    private lateinit var signalView: TextView
    private lateinit var settings: LockSettings
    private lateinit var repository: BleRepository
    private lateinit var permissionButton: Button
    private lateinit var connectButton: Button
    private lateinit var statusView: TextView
    private lateinit var liveRssiView: TextView
    private lateinit var freeKeysView: TextView
    private lateinit var espOpenRssiField: EditText
    private lateinit var espHoldRssiField: EditText
    private lateinit var adminLongOpen: EditText
    private lateinit var adminTimerA: EditText
    private lateinit var adminTimerB: EditText
    private lateinit var adminLockGpio: EditText
    private lateinit var adminOpenLevel: Switch
    private lateinit var adminAdvTxPower: Spinner
    private lateinit var adminAdvTxPowerValue: TextView
    private lateinit var adminSaveButton: Button
    private lateinit var swipeRefresh: SwipeRefreshLayout
    private lateinit var adminBondsContainer: LinearLayout
    private lateinit var adminDeleteBondButton: Button
    private lateinit var adminContainer: LinearLayout
    private val adminButtons = mutableListOf<Button>()
    private var adminModeAvailable = false
    private var serviceMenuRequested = false
    private var manualConnectionActive = false
    private var lastBleState = BleRepository.State.IDLE
    private var activityResumed = false
    private var selectedBondAddress: String? = null
    private var updatingAutoOpenSwitch = false
    private val signalHandler = Handler(Looper.getMainLooper())
    private val adminSignalPoll = object : Runnable {
        override fun run() {
            if (adminModeAvailable) {
                repository.pollAdminState()
                signalHandler.postDelayed(this, ADMIN_SIGNAL_POLL_INTERVAL_MS)
            }
        }
    }
    private val resumeService = Runnable {
        if (settings.autoOpenEnabled && hasBlePermissions()) startAutoOpenService()
    }
    private var backgroundReceiverRegistered = false
    private val backgroundConnectionReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                BleLockService.ACTION_SIGNAL_RSSI -> {
                    val valid = intent.getBooleanExtra(BleLockService.EXTRA_SIGNAL_VALID, false)
                    updateLiveRssiText(if (valid) intent.getIntExtra(BleLockService.EXTRA_SIGNAL_RSSI, 0) else null)
                }
                BleLockService.ACTION_CONNECTION_STATE -> {
                    updateLiveRssiColor(intent.getBooleanExtra(BleLockService.EXTRA_CONNECTED, false))
                }
            }
        }
    }
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        settings = LockSettings(this)
        repository = BleRepository(this, object : BleRepository.Listener {
            override fun onBleState(state: BleRepository.State, detail: String) {
                lastBleState = state
                connectButton.isEnabled = state != BleRepository.State.SCANNING &&
                    state != BleRepository.State.CONNECTING && state != BleRepository.State.BONDING
                updateAdminButtonsEnabled()
                if (state == BleRepository.State.IDLE || state == BleRepository.State.ERROR) {
                    adminContainer.visibility = View.GONE
                    swipeRefresh.isRefreshing = false
                }
                appendLog(detail)
            }

            override fun onConnectionLost() {
                signalHandler.removeCallbacks(resumeService)
                if (!manualConnectionActive && settings.autoOpenEnabled) {
                    signalHandler.postDelayed(resumeService, SERVICE_RESUME_DELAY_MS)
                }
            }

            override fun onAdminAvailability(available: Boolean) {
                val wasShown = adminContainer.visibility == View.VISIBLE
                adminModeAvailable = available
                val showAdmin = available && serviceMenuRequested
                adminContainer.visibility = if (showAdmin) View.VISIBLE else View.GONE
                signalHandler.removeCallbacks(adminSignalPoll)
                if (showAdmin) {
                    signalHandler.postDelayed(adminSignalPoll, ADMIN_SIGNAL_POLL_INTERVAL_MS)
                    if (!wasShown) repository.readAdminConfig()
                } else {
                    showSignal(null)
                }
                updateAdminButtonsEnabled()
                appendLog(if (available) "Режим настройки доступен" else "Обычный режим: настройки недоступны")
            }

            override fun onLockData(info: String?, status: String?) {
                // Скрытые значения остаются частью BLE control flow.
            }

            override fun onAdminConfig(value: String) {
                appendLog("Получена строка конфигурации: \"$value\"")
                val fields = value.split(',')
                if (fields.size !in 7..8) {
                    appendLog("Некорректный формат настроек: $value")
                    swipeRefresh.isRefreshing = false
                    return
                }
                espOpenRssiField.setText(fields[0])
                espHoldRssiField.setText(fields[1])
                val longOpenMs = fields[2].toLongOrNull()
                val longOpenMinutes = if (longOpenMs == null || longOpenMs <= 0L) {
                    0L
                } else {
                    ((longOpenMs + LONG_OPEN_MINUTE_MS - 1L) / LONG_OPEN_MINUTE_MS)
                        .coerceAtMost(MAX_LONG_OPEN_MINUTES)
                }
                adminLongOpen.setText(longOpenMinutes.toString())
                adminTimerA.setText(fields[3])
                adminTimerB.setText(fields[4])
                adminLockGpio.setText(fields[5])
                adminOpenLevel.isChecked = fields[6] == "1"
                val advPower = fields.getOrNull(7)?.toIntOrNull() ?: DEFAULT_ADV_TX_POWER_DBM
                val powerIndex = ADV_TX_POWER_VALUES.indexOf(advPower).takeIf { it >= 0 }
                    ?: ADV_TX_POWER_VALUES.indexOf(DEFAULT_ADV_TX_POWER_DBM)
                adminAdvTxPower.setSelection(powerIndex)
                adminAdvTxPowerValue.text = ADV_TX_POWER_LABELS.getOrElse(powerIndex) { "" }
                serviceMenuRequested = true
                adminModeAvailable = true
                adminContainer.visibility = View.VISIBLE
                appendLog("Настройки замка прочитаны")
                repository.readAdminBonds()
                swipeRefresh.isRefreshing = false
            }

            override fun onAdminState(value: String) {
                val rawRssi = value.substringAfter("rssi=", "").substringBefore(';')
                showSignal(rawRssi.toIntOrNull())
            }

            override fun onSignalRssi(rssi: Int?) {
                updateLiveRssiText(rssi)
            }

            override fun onAdminBonds(value: String) {
                runOnUiThread { renderBondList(value) }
            }

        })
        setContentView(createContent())
        showBatteryOptimizationNoticeIfNeeded()
        refreshState()
    }

    override fun onResume() {
        super.onResume()
        activityResumed = true
        settings.autoOpenEnabled = true
        updatingAutoOpenSwitch = true
        autoOpenSwitch.isChecked = true
        updatingAutoOpenSwitch = false
        // Экран открылся: фоновое подключение должно продолжать жить как есть,
        // поэтому сервис не перезапускаем, а только убеждаемся, что он запущен.
        if (settings.autoOpenEnabled && hasBlePermissions()) startAutoOpenService()
        registerBackgroundReceiver()
        refreshState()
    }

    override fun onPause() {
        super.onPause()
        activityResumed = false
        unregisterBackgroundReceiver()
    }

    private fun registerBackgroundReceiver() {
        if (backgroundReceiverRegistered) return
        val filter = IntentFilter().apply {
            addAction(BleLockService.ACTION_SIGNAL_RSSI)
            addAction(BleLockService.ACTION_CONNECTION_STATE)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(backgroundConnectionReceiver, filter, RECEIVER_NOT_EXPORTED)
        } else {
            registerReceiver(backgroundConnectionReceiver, filter)
        }
        backgroundReceiverRegistered = true
        // Сервис мог подключиться ещё до открытия экрана — подтягиваем
        // текущее состояние сразу, не дожидаясь следующей смены состояния.
        updateLiveRssiText(BleLockService.lastRssi)
        updateLiveRssiColor(BleLockService.lastConnected)
    }

    private fun unregisterBackgroundReceiver() {
        if (!backgroundReceiverRegistered) return
        runCatching { unregisterReceiver(backgroundConnectionReceiver) }
        backgroundReceiverRegistered = false
    }

    private fun updateLiveRssiText(rssi: Int?) {
        liveRssiView.text = if (rssi == null) {
            getString(R.string.lock_signal_unavailable)
        } else {
            getString(R.string.lock_signal, rssi)
        }
    }

    private fun updateLiveRssiColor(connected: Boolean) {
        // Без задержки — сразу цветом отражаем текущее состояние фонового подключения.
        liveRssiView.setTextColor(
            getColor(if (connected) R.color.switch_track_on else R.color.secondary_text)
        )
    }

    override fun onDestroy() {
        signalHandler.removeCallbacks(adminSignalPoll)
        signalHandler.removeCallbacks(resumeService)
        repository.close()
        if (settings.autoOpenEnabled) startAutoOpenService()
        super.onDestroy()
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == REQUEST_BLE_PERMISSIONS) {
            refreshState()
            if (settings.autoOpenEnabled && hasBlePermissions()) startAutoOpenService()
        }
    }

    private fun createContent(): View {
        val outerPadding = dp(16)
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(outerPadding, dp(12), outerPadding, dp(12))
        }

        root.addView(TextView(this).apply {
            text = getString(R.string.app_title)
            textSize = 26f
            setTypeface(typeface, Typeface.BOLD)
            setTextColor(getColor(R.color.primary_text))
        }, fullWidth().apply { bottomMargin = dp(16) })

        permissionButton = filledButton(R.string.grant_permissions) { requestMissingPermissions() }
        root.addView(permissionButton, spacedFullWidth())

        // --- ДОСТУП ---
        root.addView(sectionHeader(R.string.section_access), topSpacedFullWidth(4))
        val accessCard = newCard()
        autoOpenSwitch = Switch(this)
        tintSwitch(autoOpenSwitch)
        autoOpenSwitch.isChecked = settings.autoOpenEnabled
        autoOpenSwitch.setOnCheckedChangeListener { _, checked ->
            if (updatingAutoOpenSwitch) return@setOnCheckedChangeListener
            settings.autoOpenEnabled = checked
            if (checked && hasBlePermissions()) startAutoOpenService()
            else if (!checked) {
                stopAutoOpenService()
            }
            refreshState()
        }
        addRow(accessCard, switchRow(getString(R.string.auto_open), autoOpenSwitch))

        addRow(accessCard, localSliderRow(
            getString(R.string.connect_rssi_threshold),
            min = -100,
            max = 0,
            step = 1,
            initial = settings.connectRssiThreshold.toLong()
        ) { value -> settings.connectRssiThreshold = value.toInt() })

        addRow(accessCard, localSliderRow(
            getString(R.string.disconnect_rssi_threshold),
            min = -100,
            max = 0,
            step = 1,
            initial = settings.disconnectRssiThreshold.toLong()
        ) { value -> settings.disconnectRssiThreshold = value.toInt() })

        liveRssiView = TextView(this).apply {
            text = getString(R.string.lock_signal_unavailable)
            textSize = 16f
            setTypeface(typeface, Typeface.BOLD)
            setTextColor(getColor(R.color.secondary_text))
        }
        addRow(accessCard, valueRow(getString(R.string.live_rssi_label), liveRssiView))

        connectButton = navigationButton(R.string.register_phone) { activateServiceMenu() }
        addRow(accessCard, connectButton)
        root.addView(accessCard, spacedFullWidth())

        statusView = TextView(this).apply {
            textSize = 13f
            setTextColor(getColor(R.color.secondary_text))
        }
        root.addView(statusView, topSpacedFullWidth(8).apply { leftMargin = dp(4) })

        // --- Admin container: everything that only appears once the lock exposes admin mode ---
        adminContainer = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            visibility = View.GONE
        }
        root.addView(adminContainer, fullWidth())

        adminContainer.addView(TextView(this).apply {
            text = getString(R.string.admin_title)
            textSize = 20f
            setTypeface(typeface, Typeface.BOLD)
            setTextColor(getColor(R.color.primary_text))
        }, topSpacedFullWidth(20))

        // --- RSSI ---
        adminContainer.addView(sectionHeader(R.string.section_rssi), topSpacedFullWidth(12))
        val rssiCard = newCard()
        signalView = TextView(this).apply {
            text = getString(R.string.lock_signal_unavailable)
            textSize = 17f
            setTypeface(typeface, Typeface.BOLD)
            setTextColor(getColor(R.color.secondary_text))
        }
        addRow(rssiCard, valueRow(getString(R.string.current_rssi_label), signalView))
        espOpenRssiField = adminNumberField(true)
        espHoldRssiField = adminNumberField(true)
        addRow(rssiCard, stepperRow(getString(R.string.esp_open_rssi_threshold), espOpenRssiField, -100, 0, 1))
        addRow(rssiCard, stepperRow(getString(R.string.esp_hold_rssi_threshold), espHoldRssiField, -100, 0, 1))
        adminContainer.addView(rssiCard, fullWidth())

        // --- ТАЙМЕРЫ ---
        adminContainer.addView(sectionHeader(R.string.section_timers), topSpacedFullWidth(20))
        val timersCard = newCard()
        adminLongOpen = adminNumberField(false)
        adminTimerA = adminNumberField(false)
        adminTimerB = adminNumberField(false)
        addRow(timersCard, stepperRow(
            getString(R.string.admin_long_open), adminLongOpen,
            0, MAX_LONG_OPEN_MINUTES, 1
        ))
        addRow(timersCard, stepperRow(getString(R.string.admin_timer_a), adminTimerA, 0, 10_000, 100))
        addRow(timersCard, stepperRow(getString(R.string.admin_timer_b), adminTimerB, 0, 120, 1))
        adminContainer.addView(timersCard, fullWidth())

        // --- АППАРАТНАЯ ЧАСТЬ ---
        adminContainer.addView(sectionHeader(R.string.section_hardware), topSpacedFullWidth(20))
        val hardwareCard = newCard()
        adminLockGpio = adminNumberField(false)
        addRow(hardwareCard, stepperRow(getString(R.string.admin_lock_gpio), adminLockGpio, 0, 39, 1))
        adminOpenLevel = Switch(this)
        tintSwitch(adminOpenLevel)
        addRow(hardwareCard, switchRow(getString(R.string.admin_open_level), adminOpenLevel))
        adminAdvTxPower = Spinner(this).apply {
            adapter = ArrayAdapter(
                this@MainActivity,
                android.R.layout.simple_spinner_item,
                ADV_TX_POWER_LABELS
            ).also { it.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item) }
            setSelection(ADV_TX_POWER_VALUES.indexOf(DEFAULT_ADV_TX_POWER_DBM))
            alpha = 0f
            onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
                override fun onItemSelected(parent: AdapterView<*>?, view: View?, position: Int, id: Long) {
                    adminAdvTxPowerValue.text = ADV_TX_POWER_LABELS.getOrElse(position) { "" }
                }
                override fun onNothingSelected(parent: AdapterView<*>?) {}
            }
        }
        adminAdvTxPowerValue = TextView(this).apply {
            text = ADV_TX_POWER_LABELS[ADV_TX_POWER_VALUES.indexOf(DEFAULT_ADV_TX_POWER_DBM)]
        }
        addRow(hardwareCard, disclosureRow(getString(R.string.admin_adv_tx_power), adminAdvTxPowerValue, adminAdvTxPower))
        adminContainer.addView(hardwareCard, fullWidth())

        // --- ДЕЙСТВИЯ ---
        adminContainer.addView(sectionHeader(R.string.section_actions), topSpacedFullWidth(20))
        adminSaveButton = filledButton(R.string.admin_save) { saveAdminConfig() }
        adminSaveButton.isEnabled = false
        adminContainer.addView(adminSaveButton, spacedFullWidth())

        // --- ЗАРЕГИСТРИРОВАННЫЕ ТЕЛЕФОНЫ ---
        adminContainer.addView(sectionHeader(R.string.section_bonds), topSpacedFullWidth(20))
        adminBondsContainer = newCard()
        adminContainer.addView(adminBondsContainer, fullWidth())
        freeKeysView = TextView(this).apply {
            textSize = 13f
            setTextColor(getColor(R.color.secondary_text))
        }
        adminContainer.addView(freeKeysView, topSpacedFullWidth(8).apply {
            leftMargin = dp(4)
        })
        val bondActionsCard = newCard()
        adminDeleteBondButton = destructiveButton(R.string.admin_delete_bond) {
            selectedBondAddress?.let { repository.deleteBond(it) }
        }
        adminDeleteBondButton.isEnabled = false
        addRow(bondActionsCard, adminDeleteBondButton)
        adminContainer.addView(bondActionsCard, topSpacedFullWidth(10))

        // --- ОПАСНАЯ ЗОНА ---
        adminContainer.addView(sectionHeader(R.string.section_danger), topSpacedFullWidth(20))
        val dangerCard = newCard()
        addRow(dangerCard, addAdminButton(R.string.admin_delete_all) {
            appendLog("После удаления всех bonds удалите BLE Lock в настройках Bluetooth телефона")
            repository.deleteAllBonds()
        })
        addRow(dangerCard, addAdminButton(R.string.admin_reboot) {
            appendLog("Команда перезагрузки ESP32")
            repository.rebootEsp32()
        })
        adminContainer.addView(dangerCard, fullWidth())

        adminContainer.addView(Space(this), fullWidth().apply { height = dp(32) })

        val scroll = ScrollView(this).apply {
            isFillViewport = true
            setBackgroundColor(Color.TRANSPARENT)
            addView(root)
        }
        swipeRefresh = SwipeRefreshLayout(this).apply {
            setColorSchemeResources(R.color.accent_blue)
            setOnRefreshListener {
                if (adminModeAvailable) {
                    repository.readAdminConfig()
                    signalHandler.postDelayed({ isRefreshing = false }, SWIPE_REFRESH_TIMEOUT_MS)
                } else {
                    isRefreshing = false
                }
            }
            addView(scroll, ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
        }
        return FrameLayout(this).apply {
            setBackgroundColor(getColor(R.color.screen_background))
            addView(swipeRefresh, FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            ))
        }
    }

    // ---------- Layout helpers (grouped-card / iOS-style settings look) ----------

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density).toInt()

    private fun rowPadding(view: View) {
        view.setPadding(dp(16), dp(12), dp(16), dp(12))
    }

    private fun newCard(): LinearLayout = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        background = getDrawable(R.drawable.bg_card)
    }

    private fun sectionHeader(textRes: Int): TextView = TextView(this).apply {
        text = getString(textRes)
        textSize = 13f
        setTextColor(getColor(R.color.section_header_text))
        setPadding(dp(4), 0, dp(4), dp(6))
    }

    private fun addRow(container: LinearLayout, row: View) {
        if (container.childCount > 0) {
            container.addView(View(this).apply {
                setBackgroundColor(getColor(R.color.divider))
            }, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(1)).apply {
                marginStart = dp(16)
            })
        }
        container.addView(row, fullWidth())
    }

    private fun rowLabel(text: String): TextView = TextView(this).apply {
        this.text = text
        textSize = 16f
        setTextColor(getColor(R.color.primary_text))
        layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
    }

    private fun valueRow(label: String, valueView: TextView): View = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
        rowPadding(this)
        addView(rowLabel(label))
        addView(valueView)
    }

    private fun switchRow(label: String, switch: Switch): View = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
        rowPadding(this)
        addView(rowLabel(label))
        addView(switch)
    }

    private fun disclosureRow(label: String, valueView: TextView, spinner: Spinner): View {
        val valueContainer = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            addView(valueView.apply {
                textSize = 16f
                setTextColor(getColor(R.color.secondary_text))
            })
            addView(TextView(this@MainActivity).apply {
                text = "\u203A"
                textSize = 18f
                setTextColor(getColor(R.color.secondary_text))
                setPadding(dp(6), 0, 0, 0)
            })
            addView(spinner, LinearLayout.LayoutParams(dp(1), dp(1)))
        }
        return LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            rowPadding(this)
            isClickable = true
            isFocusable = true
            background = selectableBackground()
            setOnClickListener { spinner.performClick() }
            addView(rowLabel(label))
            addView(valueContainer)
        }
    }

    private fun stepperRow(label: String, editText: EditText, min: Long, max: Long, step: Long): View {
        editText.visibility = View.GONE

        val valueLabel = TextView(this).apply {
            textSize = 16f
            setTypeface(typeface, Typeface.BOLD)
            setTextColor(getColor(R.color.accent_blue))
        }

        val steps = ((max - min) / step).toInt().coerceAtLeast(1)
        val seekBar = SeekBar(this)
        seekBar.max = steps

        var updating = false
        fun applyValue(raw: Long) {
            val stepped = min + ((raw - min).coerceIn(0, max - min) / step) * step
            val clamped = stepped.coerceIn(min, max)
            updating = true
            seekBar.progress = ((clamped - min) / step).toInt()
            valueLabel.text = clamped.toString()
            if (editText.text.toString() != clamped.toString()) {
                editText.setText(clamped.toString())
            }
            updating = false
        }

        seekBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(bar: SeekBar?, progress: Int, fromUser: Boolean) {
                if (!fromUser || updating) return
                applyValue(min + progress * step)
            }
            override fun onStartTrackingTouch(bar: SeekBar?) {}
            override fun onStopTrackingTouch(bar: SeekBar?) {}
        })
        editText.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: Editable?) {
                if (updating) return
                val value = s.toString().toLongOrNull() ?: return
                applyValue(value)
            }
        })

        applyValue(editText.text.toString().toLongOrNull() ?: min)

        val header = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            addView(rowLabel(label))
            addView(valueLabel)
        }
        return LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            rowPadding(this)
            addView(header)
            addView(seekBar, fullWidth().apply { topMargin = dp(4) })
            addView(editText)
        }
    }

    private fun localSliderRow(
        label: String,
        min: Long,
        max: Long,
        step: Long,
        initial: Long,
        onChange: (Long) -> Unit
    ): View {
        val valueLabel = TextView(this).apply {
            textSize = 16f
            setTypeface(typeface, Typeface.BOLD)
            setTextColor(getColor(R.color.accent_blue))
        }
        val steps = ((max - min) / step).toInt().coerceAtLeast(1)
        val seekBar = SeekBar(this)
        seekBar.max = steps

        fun show(value: Long) {
            val clamped = value.coerceIn(min, max)
            seekBar.progress = ((clamped - min) / step).toInt()
            valueLabel.text = clamped.toString()
        }
        show(initial)

        seekBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(bar: SeekBar?, progress: Int, fromUser: Boolean) {
                if (!fromUser) return
                val value = (min + progress * step).coerceIn(min, max)
                valueLabel.text = value.toString()
                onChange(value)
            }
            override fun onStartTrackingTouch(bar: SeekBar?) {}
            override fun onStopTrackingTouch(bar: SeekBar?) {}
        })

        val header = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            addView(rowLabel(label))
            addView(valueLabel)
        }
        return LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            rowPadding(this)
            addView(header)
            addView(seekBar, fullWidth().apply { topMargin = dp(4) })
        }
    }

    private fun selectableBackground(): android.graphics.drawable.Drawable? {
        val outValue = TypedValue()
        theme.resolveAttribute(android.R.attr.selectableItemBackground, outValue, true)
        return getDrawable(outValue.resourceId)
    }

    private fun styledButton(textRes: Int): Button = Button(this).apply {
        text = getString(textRes)
        isAllCaps = false
        textSize = 16f
        stateListAnimator = null
        setPadding(dp(16), dp(12), dp(16), dp(12))
    }

    private fun filledButton(textRes: Int, onClick: () -> Unit): Button = styledButton(textRes).apply {
        background = getDrawable(R.drawable.bg_button_filled)
        setTextColor(Color.WHITE)
        gravity = Gravity.CENTER
        setTypeface(typeface, Typeface.BOLD)
        setOnClickListener { onClick() }
    }

    private fun outlineButton(textRes: Int, onClick: () -> Unit): Button = styledButton(textRes).apply {
        background = getDrawable(R.drawable.bg_button_outline)
        setTextColor(getColor(R.color.accent_blue))
        gravity = Gravity.CENTER
        setTypeface(typeface, Typeface.BOLD)
        setOnClickListener { onClick() }
    }

    private fun navigationButton(textRes: Int, onClick: () -> Unit): Button = styledButton(textRes).apply {
        background = selectableBackground()
        setTextColor(getColor(R.color.accent_blue))
        gravity = Gravity.CENTER_VERTICAL or Gravity.START
        setOnClickListener { onClick() }
    }

    private fun destructiveButton(textRes: Int, onClick: () -> Unit): Button = styledButton(textRes).apply {
        background = selectableBackground()
        setTextColor(getColor(R.color.danger_red))
        gravity = Gravity.CENTER_VERTICAL or Gravity.START
        setOnClickListener { onClick() }
    }

    private fun tintSwitch(switch: Switch) {
        val states = arrayOf(
            intArrayOf(android.R.attr.state_checked),
            intArrayOf(-android.R.attr.state_checked)
        )
        switch.trackTintList = ColorStateList(
            states,
            intArrayOf(getColor(R.color.switch_track_on), getColor(R.color.switch_track_off))
        )
        switch.thumbTintList = ColorStateList(
            states,
            intArrayOf(getColor(R.color.switch_thumb), getColor(R.color.switch_thumb))
        )
    }

    private fun refreshState() {
        val permissionsGranted = hasBlePermissions()
        permissionButton.visibility = if (permissionsGranted) View.GONE else View.VISIBLE
        connectButton.visibility = if (permissionsGranted) View.VISIBLE else View.GONE
        autoOpenSwitch.isChecked = settings.autoOpenEnabled
        updateAdminButtonsEnabled()
    }

    private fun updateAdminButtonsEnabled() {
        val adminEnabled = lastBleState == BleRepository.State.READY && adminModeAvailable
        adminSaveButton.isEnabled = adminEnabled
        adminButtons.forEach { it.isEnabled = adminEnabled }
        adminDeleteBondButton.isEnabled = adminEnabled && selectedBondAddress != null
    }

    private fun hasBlePermissions(): Boolean = requiredPermissions().all {
        checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED
    }

    private fun requestMissingPermissions() {
        val missing = requiredPermissions().filter {
            checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED
        }
        if (missing.isNotEmpty()) {
            requestPermissions(missing.toTypedArray(), REQUEST_BLE_PERMISSIONS)
        } else if (settings.autoOpenEnabled) {
            startAutoOpenService()
        }
    }

    private fun startAutoOpenService() {
        if (!hasBlePermissions()) return
        val intent = Intent(this, BleLockService::class.java)
            .setAction(BleLockService.ACTION_START)
        // Activity is in the foreground here, so startService avoids the
        // foreground-service startup race on devices that aggressively
        // schedule startForegroundService() callbacks.
        startService(intent)
    }

    private fun startServiceConnection() {
        // Manual pairing/admin access must use the Activity connection only;
        // a running auto-open service would create a second SMP flow.
        manualConnectionActive = true
        signalHandler.removeCallbacks(resumeService)
        stopAutoOpenService()
        if (hasBlePermissions()) {
            appendLog("Подключение к замку…")
            signalHandler.postDelayed(
                { repository.findAndConnect() },
                MANUAL_CONNECT_DELAY_MS
            )
        } else {
            requestMissingPermissions()
        }
    }

    private fun activateServiceMenu() {
        serviceMenuRequested = true
        if (adminModeAvailable) {
            adminContainer.visibility = View.VISIBLE
            signalHandler.removeCallbacks(adminSignalPoll)
            signalHandler.postDelayed(adminSignalPoll, ADMIN_SIGNAL_POLL_INTERVAL_MS)
            updateAdminButtonsEnabled()
            repository.readAdminConfig()
        } else {
            startServiceConnection()
        }
    }

    private fun showBatteryOptimizationNoticeIfNeeded() {
        if (settings.batteryOptimizationNoticeShown) {
            showAutoStartNoticeIfNeeded()
            return
        }
        settings.batteryOptimizationNoticeShown = true
        AlertDialog.Builder(this)
            .setTitle(R.string.battery_notice_title)
            .setMessage(R.string.battery_notice_message)
            .setPositiveButton(R.string.battery_notice_open_settings) { _, _ ->
                requestIgnoreBatteryOptimizations()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .setOnDismissListener { showAutoStartNoticeIfNeeded() }
            .show()
    }

    private fun showAutoStartNoticeIfNeeded() {
        if (settings.autoStartNoticeShown || !isMiuiDevice()) return
        val intent = Intent().apply {
            component = ComponentName(
                "com.miui.securitycenter",
                "com.miui.permcenter.autostart.AutoStartManagementActivity"
            )
        }
        if (intent.resolveActivity(packageManager) == null) return
        settings.autoStartNoticeShown = true
        AlertDialog.Builder(this)
            .setTitle(R.string.autostart_notice_title)
            .setMessage(R.string.autostart_notice_message)
            .setPositiveButton(R.string.autostart_notice_open_settings) { _, _ ->
                runCatching { startActivity(intent) }
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun isMiuiDevice(): Boolean {
        val manufacturer = Build.MANUFACTURER.lowercase()
        return manufacturer.contains("xiaomi") || manufacturer.contains("redmi") ||
            manufacturer.contains("poco")
    }

    private fun requestIgnoreBatteryOptimizations() {
        val powerManager = getSystemService(PowerManager::class.java)
        if (powerManager?.isIgnoringBatteryOptimizations(packageName) == true) return
        startActivity(Intent(
            Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS,
            Uri.parse("package:$packageName")
        ))
    }

    private fun stopAutoOpenService() {
        stopService(Intent(this, BleLockService::class.java))
    }

    private fun appendLog(message: String) {
        android.util.Log.d("BleLockUi", message)
        statusView.text = message
    }

    private fun showSignal(rssi: Int?) {
        signalView.text = if (rssi == null) {
            getString(R.string.lock_signal_unavailable)
        } else {
            getString(R.string.lock_signal, rssi)
        }
    }

    private fun adminNumberField(signed: Boolean = false) = EditText(this).apply {
        isSingleLine = true
        inputType = InputType.TYPE_CLASS_NUMBER or
            if (signed) InputType.TYPE_NUMBER_FLAG_SIGNED else 0
    }

    private fun saveAdminConfig() {
        val values = listOf(
            espOpenRssiField.text.toString(), espHoldRssiField.text.toString(),
            adminLongOpen.text.toString().toLongOrNull()
                ?.coerceIn(0L, MAX_LONG_OPEN_MINUTES)
                ?.times(LONG_OPEN_MINUTE_MS)
                ?.toString() ?: "", adminTimerA.text.toString(),
            adminTimerB.text.toString(),
            adminLockGpio.text.toString()
        )
        if (values.any { it.toLongOrNull() == null }) {
            appendLog("Заполните все числовые поля настроек")
            return
        }
        val advTxPower = ADV_TX_POWER_VALUES.getOrElse(adminAdvTxPower.selectedItemPosition) {
            DEFAULT_ADV_TX_POWER_DBM
        }
        val config = values.joinToString(",") +
            (if (adminOpenLevel.isChecked) ",1" else ",0") +
            ",$advTxPower"
        repository.writeAdminConfig(config)
    }

    private fun renderBondList(value: String) {
        val bonds = value.lineSequence().mapNotNull { line ->
            val fields = line.split(',', limit = 3)
            if (fields.size < 2) null else fields[1] to fields.getOrElse(2) { "имя не сохранено" }
        }.toList()
        if (selectedBondAddress !in bonds.map { it.first }) selectedBondAddress = null
        adminBondsContainer.removeAllViews()
        if (bonds.isEmpty()) {
            adminBondsContainer.addView(TextView(this).apply {
                text = getString(R.string.admin_bonds_empty)
                textSize = 15f
                setTextColor(getColor(R.color.secondary_text))
                rowPadding(this)
            }, fullWidth())
        } else {
            bonds.forEach { (address, name) ->
                val row = TextView(this).apply {
                    text = "$name — $address"
                    textSize = 16f
                    setTextColor(getColor(R.color.primary_text))
                    rowPadding(this)
                    isClickable = true
                    isFocusable = true
                    setBackgroundColor(
                        if (address == selectedBondAddress) getColor(R.color.row_selected)
                        else Color.TRANSPARENT
                    )
                    setOnClickListener {
                        selectedBondAddress = address
                        renderBondList(value)
                    }
                }
                addRow(adminBondsContainer, row)
            }
        }
        val freeKeys = (MAX_BONDED_KEYS - bonds.size).coerceAtLeast(0)
        freeKeysView.visibility = if (freeKeys > 0) View.VISIBLE else View.GONE
        if (freeKeys > 0) {
            freeKeysView.text = getString(R.string.admin_free_keys, freeKeys)
        }
        updateAdminButtonsEnabled()
    }

    private fun addAdminButton(textRes: Int, action: () -> Unit): Button {
        val button = destructiveButton(textRes) { action() }
        button.isEnabled = false
        adminButtons += button
        return button
    }


    private fun requiredPermissions(): List<String> {
        val permissions = mutableListOf<String>()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            permissions += Manifest.permission.BLUETOOTH_SCAN
            permissions += Manifest.permission.BLUETOOTH_CONNECT
        } else {
            permissions += Manifest.permission.ACCESS_FINE_LOCATION
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            permissions += Manifest.permission.POST_NOTIFICATIONS
        }
        return permissions
    }

    private fun fullWidth() = LinearLayout.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.WRAP_CONTENT
    )

    private fun spacedFullWidth() = fullWidth().apply {
        topMargin = (14 * resources.displayMetrics.density).toInt()
    }

    private fun topSpacedFullWidth(topDp: Int) = fullWidth().apply {
        topMargin = dp(topDp)
    }

    companion object {
        private const val LONG_OPEN_MINUTE_MS = 60_000L
        private const val MAX_LONG_OPEN_MINUTES = 60L
        private const val REQUEST_BLE_PERMISSIONS = 100
        private const val ADMIN_SIGNAL_POLL_INTERVAL_MS = 2_000L
        private const val SERVICE_RESUME_DELAY_MS = 700L
        private const val MANUAL_CONNECT_DELAY_MS = 1_500L
        private const val SWIPE_REFRESH_TIMEOUT_MS = 4_000L
        private const val MAX_BONDED_KEYS = 3
        private const val DEFAULT_ADV_TX_POWER_DBM = 0
        private val ADV_TX_POWER_VALUES = intArrayOf(-12, -9, -6, -3, 0, 3, 6, 9)
        private val ADV_TX_POWER_LABELS = arrayOf(
            "−12 dBm", "−9 dBm", "−6 dBm", "−3 dBm",
            "0 dBm", "+3 dBm", "+6 dBm", "+9 dBm"
        )
    }
}
