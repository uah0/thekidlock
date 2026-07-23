package com.blelock.app

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.content.ComponentName
import android.content.Context
import android.graphics.Color
import android.content.pm.PackageManager
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.net.Uri
import android.provider.Settings
import android.text.InputType
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.Switch
import android.widget.TextView

class MainActivity : Activity() {
    private lateinit var autoOpenSwitch: Switch
    private lateinit var signalView: TextView
    private lateinit var settings: LockSettings
    private lateinit var repository: BleRepository
    private lateinit var permissionButton: Button
    private lateinit var connectButton: Button
    private lateinit var freeKeysView: TextView
    private lateinit var adminRssi: EditText
    private lateinit var adminRssiHold: EditText
    private lateinit var adminLongOpen: EditText
    private lateinit var adminTimerA: EditText
    private lateinit var adminTimerB: EditText
    private lateinit var adminLockGpio: EditText
    private lateinit var adminOpenLevel: Switch
    private lateinit var adminReadButton: Button
    private lateinit var adminSaveButton: Button
    private lateinit var adminBondsContainer: LinearLayout
    private lateinit var adminDeleteBondButton: Button
    private lateinit var adminContainer: LinearLayout
    private val adminButtons = mutableListOf<Button>()
    private var adminModeAvailable = false
    private var serviceMenuRequested = false
    private var manualConnectionActive = false
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
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        settings = LockSettings(this)
        repository = BleRepository(this, object : BleRepository.Listener {
            override fun onBleState(state: BleRepository.State, detail: String) {
                connectButton.isEnabled = state != BleRepository.State.SCANNING &&
                    state != BleRepository.State.CONNECTING && state != BleRepository.State.BONDING
                val adminEnabled = state == BleRepository.State.READY && adminModeAvailable
                adminReadButton.isEnabled = adminEnabled
                adminSaveButton.isEnabled = adminEnabled
                adminButtons.forEach { it.isEnabled = adminEnabled }
                adminDeleteBondButton.isEnabled = adminEnabled && selectedBondAddress != null
                if (state == BleRepository.State.IDLE || state == BleRepository.State.ERROR) {
                    adminContainer.visibility = View.GONE
                    adminReadButton.isEnabled = false
                    adminSaveButton.isEnabled = false
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
                adminModeAvailable = available
                val showAdmin = available && serviceMenuRequested
                adminContainer.visibility = if (showAdmin) View.VISIBLE else View.GONE
                signalHandler.removeCallbacks(adminSignalPoll)
                if (showAdmin) {
                    signalHandler.postDelayed(adminSignalPoll, ADMIN_SIGNAL_POLL_INTERVAL_MS)
                } else {
                    showSignal(null)
                }
                adminReadButton.isEnabled = available
                adminSaveButton.isEnabled = available
                adminButtons.forEach { it.isEnabled = available }
                adminDeleteBondButton.isEnabled = available && selectedBondAddress != null
                appendLog(if (available) "Режим настройки доступен" else "Обычный режим: настройки недоступны")
            }

            override fun onLockData(info: String?, status: String?) {
                // Скрытые значения остаются частью BLE control flow.
            }

            override fun onAdminConfig(value: String) {
                val fields = value.split(',')
                if (fields.size != 7) {
                    appendLog("Некорректный формат настроек: $value")
                    return
                }
                adminRssi.setText(fields[0])
                adminRssiHold.setText(fields[1])
                adminLongOpen.setText(fields[2])
                adminTimerA.setText(fields[3])
                adminTimerB.setText(fields[4])
                adminLockGpio.setText(fields[5])
                adminOpenLevel.isChecked = fields[6] == "1"
                appendLog("Настройки замка прочитаны")
            }

            override fun onAdminState(value: String) {
                val rawRssi = value.substringAfter("rssi=", "").substringBefore(';')
                showSignal(rawRssi.toIntOrNull())
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
        settings.autoOpenEnabled = true
        updatingAutoOpenSwitch = true
        autoOpenSwitch.isChecked = true
        updatingAutoOpenSwitch = false
        restartAutoOpenService()
        refreshState()
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
        val padding = (20 * resources.displayMetrics.density).toInt()
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
            setPadding(padding, padding, padding, padding)
        }

        root.addView(TextView(this).apply {
            text = getString(R.string.app_title)
            textSize = 28f
            gravity = Gravity.CENTER
        }, fullWidth())

        permissionButton = Button(this).apply {
            text = getString(R.string.grant_permissions)
            setOnClickListener { requestMissingPermissions() }
        }
        root.addView(permissionButton, spacedFullWidth())

        autoOpenSwitch = Switch(this).apply {
            text = getString(R.string.auto_open)
            isChecked = settings.autoOpenEnabled
            setOnCheckedChangeListener { _, checked ->
                if (updatingAutoOpenSwitch) return@setOnCheckedChangeListener
                settings.autoOpenEnabled = checked
                if (checked && hasBlePermissions()) startAutoOpenService()
                else if (!checked) {
                    stopAutoOpenService()
                }
                refreshState()
            }
        }
        root.addView(autoOpenSwitch, spacedFullWidth())

        connectButton = Button(this).apply {
            text = getString(R.string.register_phone)
            setOnClickListener { activateServiceMenu() }
        }
        root.addView(connectButton, spacedFullWidth())

        adminContainer = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            visibility = View.GONE
        }
        root.addView(adminContainer, fullWidth())

        adminContainer.addView(TextView(this).apply {
            text = getString(R.string.admin_title)
            textSize = 20f
        }, spacedFullWidth())

        signalView = TextView(this).apply {
            text = getString(R.string.lock_signal_unavailable)
            gravity = Gravity.CENTER
            textSize = 32f
        }
        adminContainer.addView(signalView, spacedFullWidth())

        adminRssi = adminNumberField(R.string.admin_rssi, true)
        adminRssiHold = adminNumberField(R.string.admin_rssi_hold, true)
        adminLongOpen = adminNumberField(R.string.admin_long_open)
        adminTimerA = adminNumberField(R.string.admin_timer_a)
        adminTimerB = adminNumberField(R.string.admin_timer_b)
        adminLockGpio = adminNumberField(R.string.admin_lock_gpio)
        adminContainer.addView(adminRssi, spacedFullWidth())
        adminContainer.addView(adminRssiHold, spacedFullWidth())
        adminContainer.addView(adminLongOpen, spacedFullWidth())
        adminContainer.addView(adminTimerA, spacedFullWidth())
        adminContainer.addView(adminTimerB, spacedFullWidth())
        adminContainer.addView(adminLockGpio, spacedFullWidth())
        adminOpenLevel = Switch(this).apply { text = getString(R.string.admin_open_level) }
        adminContainer.addView(adminOpenLevel, spacedFullWidth())

        adminReadButton = Button(this).apply {
            text = getString(R.string.admin_read)
            isEnabled = false
            setOnClickListener { repository.readAdminConfig() }
        }
        adminContainer.addView(adminReadButton, spacedFullWidth())
        adminSaveButton = Button(this).apply {
            text = getString(R.string.admin_save)
            isEnabled = false
            setOnClickListener { saveAdminConfig() }
        }
        adminContainer.addView(adminSaveButton, spacedFullWidth())

        adminContainer.addView(TextView(this).apply {
            text = getString(R.string.admin_bonds_title)
            textSize = 22f
            gravity = Gravity.CENTER
        }, spacedFullWidth())
        adminBondsContainer = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
        }
        adminContainer.addView(adminBondsContainer, fullWidth())
        freeKeysView = TextView(this).apply {
            textSize = 20f
            gravity = Gravity.CENTER
        }
        adminContainer.addView(freeKeysView, spacedFullWidth())
        adminDeleteBondButton = Button(this).apply {
            text = getString(R.string.admin_delete_bond)
            isEnabled = false
            setOnClickListener {
                selectedBondAddress?.let { repository.deleteBond(it) }
            }
        }
        adminContainer.addView(adminDeleteBondButton, spacedFullWidth())
        addAdminButton(adminContainer, R.string.admin_delete_all) {
            appendLog("После удаления всех bonds удалите BLE Lock в настройках Bluetooth телефона")
            repository.deleteAllBonds()
        }
        val exitButton = addAdminButton(adminContainer, R.string.admin_reboot) {
            appendLog("Команда перезагрузки ESP32")
            repository.rebootEsp32()
        }
        val exitSpacer = View(this)
        adminContainer.addView(exitSpacer, fullWidth())
        exitButton.addOnLayoutChangeListener { _, _, _, _, _, _, _, _, _ ->
            if (exitButton.height > 0 && exitSpacer.layoutParams.height != exitButton.height) {
                exitSpacer.layoutParams = exitSpacer.layoutParams.apply {
                    height = exitButton.height
                }
            }
        }

        val scroll = ScrollView(this).apply {
            isFillViewport = true
            setBackgroundColor(Color.TRANSPARENT)
            addView(root)
        }
        return FrameLayout(this).apply {
            setBackgroundColor(getColor(R.color.screen_background))
            addView(scroll, FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            ))
        }
    }

    private fun refreshState() {
        val permissionsGranted = hasBlePermissions()
        permissionButton.visibility = if (permissionsGranted) View.GONE else View.VISIBLE
        connectButton.visibility = if (permissionsGranted) View.VISIBLE else View.GONE
        autoOpenSwitch.isChecked = settings.autoOpenEnabled
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

    private fun restartAutoOpenService() {
        signalHandler.removeCallbacks(resumeService)
        stopAutoOpenService()
        signalHandler.postDelayed(resumeService, SERVICE_RESTART_DELAY_MS)
    }

    private fun startServiceConnection() {
        // Manual pairing/admin access must use the Activity connection only;
        // a running auto-open service would create a second SMP flow.
        manualConnectionActive = true
        signalHandler.removeCallbacks(resumeService)
        stopAutoOpenService()
        if (hasBlePermissions()) {
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
    }

    private fun showSignal(rssi: Int?) {
        signalView.text = if (rssi == null) {
            getString(R.string.lock_signal_unavailable)
        } else {
            getString(R.string.lock_signal, rssi)
        }
    }

    private fun adminField(hintRes: Int) = EditText(this).apply {
        hint = getString(hintRes)
        isSingleLine = true
    }

    private fun adminNumberField(hintRes: Int, signed: Boolean = false) =
        adminField(hintRes).apply {
            inputType = InputType.TYPE_CLASS_NUMBER or
                if (signed) InputType.TYPE_NUMBER_FLAG_SIGNED else 0
        }

    private fun saveAdminConfig() {
        val values = listOf(
            adminRssi.text.toString(), adminRssiHold.text.toString(),
            adminLongOpen.text.toString(), adminTimerA.text.toString(),
            adminTimerB.text.toString(),
            adminLockGpio.text.toString()
        )
        if (values.any { it.toLongOrNull() == null }) {
            appendLog("Заполните все числовые поля настроек")
            return
        }
        val config = values.joinToString(",") + if (adminOpenLevel.isChecked) ",1" else ",0"
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
                textSize = 20f
                gravity = Gravity.CENTER
                setPadding(12, 20, 12, 20)
            }, fullWidth())
        } else {
            bonds.forEach { (address, name) ->
                adminBondsContainer.addView(TextView(this).apply {
                    text = "$name — $address"
                    textSize = 20f
                    gravity = Gravity.CENTER
                    setPadding(12, 24, 12, 24)
                    setBackgroundColor(
                        if (address == selectedBondAddress) Color.rgb(180, 215, 255)
                        else Color.TRANSPARENT
                    )
                    setOnClickListener {
                        selectedBondAddress = address
                        renderBondList(value)
                    }
                }, fullWidth())
            }
        }
        val freeKeys = (MAX_BONDED_KEYS - bonds.size).coerceAtLeast(0)
        freeKeysView.visibility = if (freeKeys > 0) View.VISIBLE else View.GONE
        if (freeKeys > 0) {
            freeKeysView.text = getString(R.string.admin_free_keys, freeKeys)
        }
        adminDeleteBondButton.isEnabled = adminModeAvailable && selectedBondAddress != null
    }

    private fun addAdminButton(root: LinearLayout, textRes: Int, action: () -> Unit): Button {
        val button = Button(this).apply {
            text = getString(textRes)
            isEnabled = false
            setOnClickListener { action() }
        }
        adminButtons += button
        root.addView(button, spacedFullWidth())
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

    companion object {
        private const val REQUEST_BLE_PERMISSIONS = 100
        private const val ADMIN_SIGNAL_POLL_INTERVAL_MS = 2_000L
        private const val SERVICE_RESUME_DELAY_MS = 700L
        private const val SERVICE_RESTART_DELAY_MS = 300L
        private const val MANUAL_CONNECT_DELAY_MS = 1_500L
        private const val MAX_BONDED_KEYS = 10
    }
}
