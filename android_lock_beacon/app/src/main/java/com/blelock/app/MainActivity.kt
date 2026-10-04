package com.blelock.app

import android.bluetooth.BluetoothManager

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.content.BroadcastReceiver
import android.content.ComponentName
import android.content.Context
import android.content.IntentFilter
import android.content.ServiceConnection
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.Typeface
import android.content.pm.PackageManager
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.IBinder
import android.os.SystemClock
import android.net.ConnectivityManager
import android.net.Uri
import android.net.wifi.WifiManager
import android.location.LocationManager
import android.provider.Settings
import android.text.Editable
import android.text.InputType
import android.text.TextWatcher
import android.util.TypedValue
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
import android.widget.AdapterView
import android.widget.Button
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.Toast
import android.widget.SeekBar
import android.widget.Spinner
import android.widget.ArrayAdapter
import android.widget.Space
import android.widget.Switch
import android.widget.TextView
import androidx.core.content.ContextCompat
import androidx.swiperefreshlayout.widget.SwipeRefreshLayout

class MainActivity : Activity() {
    private enum class WebUiConnectionState {
        IDLE, STARTING_HOTSPOT, SENDING_WIFI_CREDENTIALS, ESP_CONNECTING,
        WAITING_WEBUI_ADDRESS, OPENING_BROWSER, COMPLETE, ERROR
    }

    private lateinit var settings: LockSettings
    private lateinit var repository: BleClient
    private lateinit var bleListener: BleRepository.Listener
    private lateinit var mainContentView: View
    private var boundService: BleLockService.LocalBinder? = null
    private var serviceBound = false
    private var beaconPowerScreenVisible = false
    private lateinit var permissionButton: Button
    private lateinit var manualOpenButton: Button
    private lateinit var manualKeepOpenButton: Button
    private lateinit var hotspotStatusView: TextView
    private lateinit var hotspotWebUrlView: TextView
    private lateinit var hotspotOpenButton: Button
    private lateinit var freeKeysView: TextView
    private lateinit var espOpenRssiField: EditText
    private lateinit var espHoldRssiField: EditText
    private lateinit var adminTimerA: EditText
    private lateinit var adminAdvTxPower: Spinner
    private lateinit var adminAdvTxPowerValue: TextView
    private lateinit var otaSsidField: EditText
    private lateinit var otaPasswordField: EditText
    private lateinit var otaUrlField: EditText
    private lateinit var adminSaveButton: Button
    private lateinit var swipeRefresh: SwipeRefreshLayout
    private lateinit var adminBondsContainer: LinearLayout
    private lateinit var adminDeleteBondButton: Button
    private lateinit var adminContainer: LinearLayout
    private lateinit var dynamicSettingsContainer: LinearLayout
    private lateinit var staticSettingsContainer: LinearLayout
    private lateinit var legacyAdvPowerContainer: LinearLayout
    private lateinit var legacyOtaContainer: LinearLayout
    private lateinit var privacyPolicyButton: TextView
    private val adminButtons = mutableListOf<Button>()
    private val dynamicInputs = mutableMapOf<String, View>()
    private val dynamicSchemaItems = mutableMapOf<String, DeclarativeUiItem>()
    private var adminModeAvailable = false
    private var serviceMenuRequested = false
    private var manualConnectionActive = false
    private var lastBleState = BleRepository.State.IDLE
    private var activityResumed = false
    private var beaconPowerSlider: View? = null
    private var swipeStartX = 0f
    private var swipeStartY = 0f
    private var swipeStartedOnSlider = false
    private var legacyLocationDialog: AlertDialog? = null
    private var selectedBondAddress: String? = null
    private var otaStartPending = false
    private var runtimeWebPending = false
    private var webUiRequestPending = false
    private var hotspotProvisioningStarted = false
    private var webUiBrowserOpen = false
    private var otaStartToastShown = false
    private var adminProbeRetries = 0
    private var manualOpenCommandPending = false
    private var keepOpenCommandPending = false
    private var keepOpenStatusExpected = false
    private var manualCommandWaitingForReady = false
    private var workingModeState = WorkingModeState()
    private var autoOpenPermissionPending = false
    private var hotspotCredentialWriteInFlight = false
    private var webUiBlePermissionPending = false
    private var webUiConnectionState = WebUiConnectionState.IDLE
    private var webUiStartedAtMs = 0L
    private var webUiCountdownStartSeconds = 6
    private val signalHandler = Handler(Looper.getMainLooper())
    private val webUiCountdown = object : Runnable {
        override fun run() {
            if (!webUiRequestPending) return
            val elapsedSeconds =
                ((SystemClock.elapsedRealtime() - webUiStartedAtMs) / 1000L).toInt()
            val remaining = (webUiCountdownStartSeconds - elapsedSeconds).coerceAtLeast(0)
            hotspotOpenButton.text = if (remaining > 0) {
                "${getString(R.string.control_settings)} · $remaining сек"
            } else {
                getString(R.string.settings_waiting_esp)
            }
            signalHandler.postDelayed(this, 250L)
        }
    }
    private val adminSignalPoll = object : Runnable {
        override fun run() {
            if (adminModeAvailable && repository.currentState == BleRepository.State.READY) {
                repository.pollAdminState()
                repository.readOtaStatus()
                signalHandler.postDelayed(this, ADMIN_SIGNAL_POLL_INTERVAL_MS)
            }
        }
    }
    private val resumeService = Runnable {
        if (settings.autoOpenEnabled && hasBlePermissions()) startAutoOpenService()
    }
    private var backgroundReceiverRegistered = false
    private var localHotspotReservation: WifiManager.LocalOnlyHotspotReservation? = null
    private var hotspotSessionId: String? = null
    private var pendingHotspotWifi: String? = null
    private var hotspotExpectedEspIp: String? = null
    private var hotspotOpenedEspIp: String? = null
    private var hotspotIpCheckPending = false
    private val hotspotNoClientWatchdog = object : Runnable {
        override fun run() {
            if (localHotspotReservation == null) return
            if (webUiBrowserOpen) {
                signalHandler.postDelayed(this, HOTSPOT_IP_CHECK_INTERVAL_MS)
                return
            }
            if (hotspotCredentialWriteInFlight)
                closeLocalHotspot("ESP32 не подключилась к provisioning маяку за 60 секунд")
            else
                closeLocalHotspot("Истекло время настройки Wi-Fi")
        }
    }
    private val backgroundConnectionReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                BleLockService.ACTION_CONNECTION_STATE -> {
                    updateLiveRssiColor(intent.getBooleanExtra(BleLockService.EXTRA_CONNECTED, false))
                }
                BleLockService.ACTION_AUTOMATIC_MODE -> {
                    renderWorkingMode()
                }
                PhoneBeacon.ACTION_OWNER_STATUS_CHANGED -> {
                    updateAdminEntryState()
                    renderWorkingMode()
                    appendLog(if (settings.ownerAccessConfirmed) "ESP32 подтвердила роль владельца" else "ESP32 подтвердила роль ключа без owner-доступа")
                }
                PhoneBeacon.ACTION_HOLD_OPEN_RESULT -> {
                    workingModeState = workingModeState.copy(alwaysOpenEnabled = settings.confirmedHoldOpen)
                    if (intent.getBooleanExtra(PhoneBeacon.EXTRA_SUCCESS, false)) {
                        appendLog(if (settings.confirmedHoldOpen) "ESP32 подтвердила постоянное открытие по BLE"
                            else "ESP32 подтвердила отмену постоянного открытия по BLE")
                    } else {
                        val error = intent.getStringExtra(PhoneBeacon.EXTRA_ERROR).orEmpty()
                        appendLog("Команда режима замка: $error")
                        Toast.makeText(this@MainActivity, error, Toast.LENGTH_LONG).show()
                    }
                    renderWorkingMode()
                }
                PhoneBeacon.ACTION_WIFI_PROVISIONING_DISCONNECTED -> {
                    pendingHotspotWifi = null
                    hotspotCredentialWriteInFlight = false
                    runtimeWebPending = true
                    setWebUiConnectionState(WebUiConnectionState.ESP_CONNECTING)
                    hotspotStatusView.text = "ESP32 получила параметры сети и подключается к Wi-Fi"
                    appendLog("ESP32 прочитала параметры hotspot; BLE-сеанс настройки завершён")
                    signalHandler.removeCallbacks(otaStatusPoll)
                }
                PhoneBeacon.ACTION_WEB_UI_IP_RECEIVED -> {
                    val ip = intent.getStringExtra(PhoneBeacon.EXTRA_WEB_UI_IP).orEmpty()
                    if (!ip.isValidIpv4()) return
                    if (!webUiRequestPending || localHotspotReservation == null || !runtimeWebPending) {
                        appendLog("Пропускаю IP ESP32 вне текущего подключения к точке телефона")
                        return
                    }
                    settings.lockWifiIp = ip
                    openWebUiAt(ip)
                }
            }
        }
    }
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        settings = LockSettings(this)
        bleListener = object : BleRepository.Listener {
            override fun onBleState(state: BleRepository.State, detail: String) {
                lastBleState = state
                when (state) {
                    BleRepository.State.OPEN_SENT -> {
                        if (manualOpenCommandPending) {
                            manualOpenCommandPending = false
                        }
                        if (keepOpenCommandPending) keepOpenStatusExpected = true
                    }
                    BleRepository.State.ERROR -> {
                        manualOpenCommandPending = false
                        keepOpenCommandPending = false
                        keepOpenStatusExpected = false
                        manualCommandWaitingForReady = false
                        hotspotCredentialWriteInFlight = false
                        resetManualButtonStates()
                    }
                    else -> Unit
                }
                if (state == BleRepository.State.READY) {
                    trySendPendingHotspotCredentials()
                    if (manualCommandWaitingForReady &&
                        (manualOpenCommandPending || keepOpenCommandPending)) {
                        manualCommandWaitingForReady = false
                        appendLog("BLE подключён, передаю ручную команду замку")
                        if (keepOpenCommandPending) repository.toggleKeepOpen()
                        else repository.forceOpenNow()
                    }
                }
                updateMainControlsEnabled()
                updateAdminButtonsEnabled()
                if (state == BleRepository.State.IDLE || state == BleRepository.State.ERROR) {
                    setAdminPanelVisible(false)
                    swipeRefresh.isRefreshing = false
                }
                appendLog(detail)
                if (detail == "Временные настройки точки доступа переданы") {
                    pendingHotspotWifi = null
                    hotspotCredentialWriteInFlight = false
                    setWebUiConnectionState(WebUiConnectionState.ESP_CONNECTING)
                    runtimeWebPending = true
                    signalHandler.removeCallbacks(otaStatusPoll)
                }
                if (detail.contains("Wi-Fi/OTA настройки сохранены") ||
                    detail.contains("Wi-Fi/OTA настройки отклонены") ||
                    detail.contains("Не удалось сохранить Wi-Fi/OTA")) {
                    Toast.makeText(this@MainActivity, detail, Toast.LENGTH_LONG).show()
                }
                if (detail.contains("Настройки точки доступа отклонены")) {
                    hotspotCredentialWriteInFlight = false
                    setWebUiConnectionState(WebUiConnectionState.ERROR)
                }
            }

            override fun onConnectionLost() {
                signalHandler.removeCallbacks(resumeService)
                adminModeAvailable = false
                manualOpenCommandPending = false
                keepOpenCommandPending = false
                keepOpenStatusExpected = false
                manualCommandWaitingForReady = false
                resetManualButtonStates()
                updateAdminEntryState()
                updateMainControlsEnabled()
                signalHandler.removeCallbacks(adminSignalPoll)
                appendLog("ESP32 недоступен")
                if (!manualConnectionActive && settings.autoOpenEnabled) {
                    signalHandler.postDelayed(resumeService, SERVICE_RESUME_DELAY_MS)
                }
            }

            override fun onAdminAvailability(available: Boolean) {
                if (available) adminProbeRetries = 0
                if (!available && serviceMenuRequested && manualConnectionActive &&
                    repository.currentState == BleRepository.State.READY &&
                    adminProbeRetries < MAX_ADMIN_PROBE_RETRIES) {
                    adminProbeRetries++
                    appendLog("Повторная проверка доступа к панели (${adminProbeRetries}/$MAX_ADMIN_PROBE_RETRIES)")
                    signalHandler.postDelayed({ repository.retryAdminProbe() }, ADMIN_PROBE_RETRY_DELAY_MS)
                    return
                }
                adminModeAvailable = available
                if (available) settings.ownerAccessConfirmed = true
                updateAdminEntryState()
                val showAdmin = available && serviceMenuRequested
                setAdminPanelVisible(showAdmin)
                signalHandler.removeCallbacks(adminSignalPoll)
                if (showAdmin) {
                    signalHandler.postDelayed(adminSignalPoll, ADMIN_SIGNAL_POLL_INTERVAL_MS)
                    if (otaStartPending) signalHandler.postDelayed({ repository.readOtaStatus() }, 500)
                }
                updateAdminButtonsEnabled()
                appendLog(if (available) "Режим настройки доступен" else "Обычный режим: настройки недоступны")
            }

            override fun onLockData(info: String?, status: String?) {
                if (!status.isNullOrBlank()) {
                    val holdOpen = when (status.trim().uppercase()) {
                        "OPEN_HOLD" -> true
                        "OPEN", "LOCKED" -> false
                        else -> workingModeState.alwaysOpenEnabled
                    }
                    workingModeState = WorkingModeReducer.reduce(
                        workingModeState, WorkingModeEvent.AlwaysOpenConfirmed(holdOpen)
                    )
                    renderWorkingMode()
                }
                if (!status.isNullOrBlank() && keepOpenStatusExpected) keepOpenStatusExpected = false
            }

            override fun onAdminConfig(value: String) {
                appendLog("Получена строка конфигурации: \"$value\"")
                val fields = value.split(',')
                if (fields.size !in 5..6) {
                    appendLog("Некорректный формат настроек: $value")
                    swipeRefresh.isRefreshing = false
                    return
                }
                espOpenRssiField.setText(fields[0])
                espHoldRssiField.setText(fields[1])
                adminTimerA.setText(fields[3])
                val advPower = fields.getOrNull(5)?.toIntOrNull() ?: DEFAULT_ADV_TX_POWER_DBM
                val powerIndex = ADV_TX_POWER_VALUES.indexOf(advPower).takeIf { it >= 0 }
                    ?: ADV_TX_POWER_VALUES.indexOf(DEFAULT_ADV_TX_POWER_DBM)
                adminAdvTxPower.setSelection(powerIndex)
                adminAdvTxPowerValue.text = ADV_TX_POWER_LABELS.getOrElse(powerIndex) { "" }
                serviceMenuRequested = true
                adminModeAvailable = true
                setAdminPanelVisible(true)
                appendLog("Настройки замка прочитаны")
                swipeRefresh.isRefreshing = false
            }

            override fun onAdminSchema(value: String) {
                runCatching { DeclarativeUiSchema.parse(value) }
                    .onSuccess { schema ->
                        renderDeclarativeSettings(schema)
                        appendLog("Декларативная схема прочитана: ${schema.sections.sumOf { it.items.size }} элементов")
                    }
                    .onFailure { error ->
                        dynamicInputs.clear()
                        dynamicSchemaItems.clear()
                        dynamicSettingsContainer.removeAllViews()
                        dynamicSettingsContainer.visibility = View.GONE
                        staticSettingsContainer.visibility = View.VISIBLE
                        legacyAdvPowerContainer.visibility = View.VISIBLE
                        legacyOtaContainer.visibility = View.VISIBLE
                        appendLog("Схема недоступна, используется статическая панель: ${error.message}")
                    }
            }

            override fun onAdminSchemaError(detail: String) {
                dynamicInputs.clear()
                dynamicSchemaItems.clear()
                dynamicSettingsContainer.removeAllViews()
                dynamicSettingsContainer.visibility = View.GONE
                staticSettingsContainer.visibility = View.VISIBLE
                legacyAdvPowerContainer.visibility = View.VISIBLE
                legacyOtaContainer.visibility = View.VISIBLE
                appendLog("Схема недоступна, используется статическая панель: $detail")
            }

            override fun onAdminState(value: String) {
                // Текущий RSSI не выводится в пользовательском интерфейсе.
            }

            override fun onSignalRssi(rssi: Int?) {
                // RSSI не выводится на главном экране.
            }

            override fun onAdminBonds(value: String) {
                runOnUiThread {
                    renderBondList(value)
                    repository.writeCurrentPhoneName()
                }
            }

            override fun onOtaConfig(value: String) {
                appendLog("OTA-ответ: длина=${value.length}, разделителей=${value.count { it == '\n' }}")
                if (value.isEmpty()) {
                    otaSsidField.setText("")
                    otaPasswordField.setText("")
                    otaUrlField.setText("")
                    (dynamicInputs["ota_wifi_ssid"] as? EditText)?.setText("")
                    (dynamicInputs["ota_wifi_password"] as? EditText)?.setText("")
                    (dynamicInputs["ota_firmware_url"] as? EditText)?.setText("")
                    appendLog("Wi-Fi/OTA настройки прочитаны: поля пустые")
                    return
                }
                val firstSeparator = value.indexOf('\n')
                if (firstSeparator < 0) {
                    otaSsidField.setText(value)
                    otaPasswordField.setText("")
                    otaUrlField.setText("")
                    (dynamicInputs["ota_wifi_ssid"] as? EditText)?.setText(value)
                    (dynamicInputs["ota_wifi_password"] as? EditText)?.setText("")
                    (dynamicInputs["ota_firmware_url"] as? EditText)?.setText("")
                    appendLog("Wi-Fi/OTA настройки прочитаны: URL отсутствует")
                    return
                }
                val rest = value.substring(firstSeparator + 1)
                val secondSeparator = rest.indexOf('\n')
                val ssid = value.substring(0, firstSeparator)
                /* Accept the former SSID/password/URL response for compatibility,
                   but never display the middle field as a password. */
                val url = if (secondSeparator >= 0) rest.substring(secondSeparator + 1)
                          else rest
                otaSsidField.setText(ssid)
                otaPasswordField.setText("")
                otaUrlField.setText(url)
                (dynamicInputs["ota_wifi_ssid"] as? EditText)?.setText(ssid)
                (dynamicInputs["ota_wifi_password"] as? EditText)?.setText("")
                (dynamicInputs["ota_firmware_url"] as? EditText)?.setText(url)
                appendLog("Wi-Fi/OTA настройки прочитаны")
            }

            override fun onOtaStatus(value: String) {
                if (value.startsWith("WEB_READY;IP=")) {
                    if (webUiRequestPending && localHotspotReservation != null) {
                        appendLog("Ожидаю IP ESP32, переданный по BLE после подключения к точке телефона")
                        return
                    }
                    hotspotIpCheckPending = false
                    signalHandler.removeCallbacks(hotspotNoClientWatchdog)
                    val ip = value.substringAfter("WEB_READY;IP=").trim()
                    if (ip.matches(Regex("\\d{1,3}(\\.\\d{1,3}){3}"))) {
                        if (localHotspotReservation == null) settings.lockWifiIp = ip
                        if (localHotspotReservation != null) {
                            val openedIp = hotspotOpenedEspIp
                            if (openedIp != null && openedIp != ip) {
                                closeLocalHotspot("IP ESP32 изменился с $openedIp на $ip")
                                webUiRequestPending = false
                                return
                            }
                            hotspotExpectedEspIp = ip
                            if (openedIp != null) signalHandler.postDelayed(
                                hotspotNoClientWatchdog, HOTSPOT_IP_CHECK_INTERVAL_MS)
                        }
                        val url = "http://$ip/"
                        hotspotWebUrlView.text = url
                        hotspotWebUrlView.visibility = View.VISIBLE
                        hotspotOpenButton.isEnabled = true
                        hotspotOpenButton.visibility = View.VISIBLE
                        hotspotWebUrlView.tag = url
                        appendLog("Web UI ESP32 доступен: $url")
                        if (webUiRequestPending) openWebUiAt(ip)
                    }
                    runtimeWebPending = false
                    signalHandler.removeCallbacks(otaStatusPoll)
                    return
                }
                if (localHotspotReservation != null && value.startsWith("ERROR_")) {
                    if (hotspotOpenedEspIp != null) {
                        closeLocalHotspot("ESP32 больше не сообщает IP открытой страницы")
                        return
                    }
                    hotspotExpectedEspIp = null
                    hotspotIpCheckPending = false
                    signalHandler.removeCallbacks(hotspotNoClientWatchdog)
                    signalHandler.postDelayed(hotspotNoClientWatchdog, HOTSPOT_PROVISIONING_TIMEOUT_MS)
                }
                if (localHotspotReservation != null && hotspotOpenedEspIp != null) {
                    closeLocalHotspot("ESP32 больше не сообщает IP открытой страницы")
                    return
                }
                if (webUiRequestPending && value == "CONNECTING_WEB") {
                    setWebUiConnectionState(WebUiConnectionState.WAITING_WEBUI_ADDRESS)
                    appendLog("ESP32 подключается к сохранённой Wi-Fi сети, ожидаю IP")
                    return
                }
                if (webUiRequestPending && !hotspotProvisioningStarted) {
                    hotspotProvisioningStarted = true
                    appendLog("IP ESP32 не присвоен, запускаю точку доступа телефона")
                    startWebUiProvisioning()
                }
                if (runtimeWebPending && value.startsWith("ERROR_")) {
                    runtimeWebPending = false
                    webUiRequestPending = false
                    hotspotProvisioningStarted = false
                    signalHandler.removeCallbacks(otaStatusPoll)
                    setWebUiConnectionState(WebUiConnectionState.ERROR)
                    appendLog("ESP32 не подключился к точке телефона: $value")
                    return
                }
                if (otaStartPending) when (value.trim()) {
                    "DOWNLOADING" -> if (!otaStartToastShown) {
                        otaStartToastShown = true
                        Toast.makeText(this@MainActivity,
                            "Прошивка начала обновляться", Toast.LENGTH_LONG).show()
                        appendLog("Получены первые байты прошивки")
                    }
                    "SUCCESS" -> {
                        appendLog("OTA завершена успешно")
                        otaStartPending = false
                        signalHandler.removeCallbacks(otaStatusPoll)
                    }
                    "ERROR_WIFI", "ERROR_OTA" -> {
                        Toast.makeText(this@MainActivity,
                            "Обновление прошивки не выполнено", Toast.LENGTH_LONG).show()
                        appendLog("OTA завершилась ошибкой: $value")
                        otaStartPending = false
                        signalHandler.removeCallbacks(otaStatusPoll)
                    }
                }
            }

        }
        mainContentView = createContent()
        setContentView(mainContentView)
        if (savedInstanceState?.getBoolean(KEY_BEACON_POWER_SCREEN_VISIBLE) == true) {
            showBeaconPowerScreen(animated = false)
        }
        showBatteryOptimizationNoticeIfNeeded()
        workingModeState = workingModeState.copy(autoOpenEnabled = settings.autoOpenEnabled,
            alwaysOpenEnabled = settings.confirmedHoldOpen)
        bindBleService()
        refreshState()
    }

    override fun dispatchTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                swipeStartX = event.rawX
                swipeStartY = event.rawY
                swipeStartedOnSlider = beaconPowerScreenVisible && isInsideBeaconPowerSlider(
                    event.rawX,
                    event.rawY
                )
            }
            MotionEvent.ACTION_UP -> {
                val dx = event.rawX - swipeStartX
                val dy = event.rawY - swipeStartY
                if (beaconPowerScreenVisible) {
                    if (!swipeStartedOnSlider && kotlin.math.hypot(dx, dy) >= dp(72)) {
                        window.decorView.post { closeBeaconPowerScreen(animated = true) }
                    }
                } else if (::mainContentView.isInitialized && !serviceMenuRequested &&
                    -dy >= dp(90) && -dy > kotlin.math.abs(dx) * 1.2f) {
                    window.decorView.post { showBeaconPowerScreen(animated = true) }
                }
                swipeStartedOnSlider = false
            }
            MotionEvent.ACTION_CANCEL -> swipeStartedOnSlider = false
        }
        return super.dispatchTouchEvent(event)
    }

    override fun onSaveInstanceState(outState: Bundle) {
        outState.putBoolean(KEY_BEACON_POWER_SCREEN_VISIBLE, beaconPowerScreenVisible)
        super.onSaveInstanceState(outState)
    }

    @Deprecated("Deprecated by Android in favor of predictive Back")
    override fun onBackPressed() {
        if (beaconPowerScreenVisible) {
            closeBeaconPowerScreen(animated = true)
        } else if (serviceMenuRequested) {
            closeServiceMenu()
        } else {
            super.onBackPressed()
        }
    }

    private fun showBeaconPowerScreen(animated: Boolean) {
        if (beaconPowerScreenVisible) return
        beaconPowerScreenVisible = true
        val screen = createBeaconPowerScreen()
        setContentView(screen)
        if (animated) {
            screen.translationY = resources.displayMetrics.heightPixels.toFloat()
            screen.animate()
                .translationY(0f)
                .setDuration(SCREEN_TRANSITION_DURATION_MS)
                .start()
        }
    }

    private fun closeBeaconPowerScreen(animated: Boolean) {
        if (!beaconPowerScreenVisible) return
        beaconPowerScreenVisible = false
        setContentView(mainContentView)
        if (animated) {
            mainContentView.translationY = -resources.displayMetrics.heightPixels.toFloat()
            mainContentView.animate()
                .translationY(0f)
                .setDuration(SCREEN_TRANSITION_DURATION_MS)
                .start()
        }
    }

    override fun onNewIntent(intent: Intent?) {
        super.onNewIntent(intent)
        if (intent != null) setIntent(intent)
    }

    override fun onResume() {
        super.onResume()
        activityResumed = true
        // bindBleService() is the single owner of connection startup while the
        // screen is visible. Starting ACTION_START here raced the Binder's
        // full-control request after a process restart and could delay the
        // visible connection until the background reconnect backoff expired.
        registerBackgroundReceiver()
        workingModeState = workingModeState.copy(alwaysOpenEnabled = settings.confirmedHoldOpen)
        refreshState()
        showLegacyBleLocationNoticeIfNeeded()
        if (webUiBrowserOpen) {
            webUiBrowserOpen = false
            if (localHotspotReservation != null) {
                closeLocalHotspot("Возврат из Web UI")
                setWebUiConnectionState(WebUiConnectionState.IDLE)
            } else {
                finishWebUiIpRequest()
            }
        }
    }

    override fun onPause() {
        super.onPause()
        activityResumed = false
        unregisterBackgroundReceiver()
    }

    override fun onStop() {
        super.onStop()
        if (settings.autoOpenEnabled) {
            // The visible screen temporarily promotes the shared BLE session to
            // ADMIN. Screen lock/backgrounding does not destroy the Activity, so
            // onDestroy() cannot be relied on to restore background automation.
            if (serviceBound) {
                boundService?.resumeAutomaticMode()
            } else {
                startAutoOpenService()
            }
        }
    }

    private fun registerBackgroundReceiver() {
        if (backgroundReceiverRegistered) return
        val filter = IntentFilter().apply {
            addAction(BleLockService.ACTION_CONNECTION_STATE)
            addAction(BleLockService.ACTION_AUTOMATIC_MODE)
            addAction(PhoneBeacon.ACTION_OWNER_STATUS_CHANGED)
            addAction(PhoneBeacon.ACTION_WIFI_PROVISIONING_DISCONNECTED)
            addAction(PhoneBeacon.ACTION_WEB_UI_IP_RECEIVED)
            addAction(PhoneBeacon.ACTION_HOLD_OPEN_RESULT)
        }
        ContextCompat.registerReceiver(
            this,
            backgroundConnectionReceiver,
            filter,
            ContextCompat.RECEIVER_NOT_EXPORTED
        )
        backgroundReceiverRegistered = true
        // Сервис мог подключиться ещё до открытия экрана — подтягиваем
        // текущее состояние сразу, не дожидаясь следующей смены состояния.
        updateLiveRssiColor(BleLockService.lastConnected)
    }

    private fun unregisterBackgroundReceiver() {
        if (!backgroundReceiverRegistered) return
        runCatching { unregisterReceiver(backgroundConnectionReceiver) }
        backgroundReceiverRegistered = false
    }

    private fun updateLiveRssiColor(connected: Boolean) {
        // RSSI больше не выводится на главном экране. Состояние соединения
        // продолжает храниться в сервисе для автоматики и административной панели.
    }

    override fun onDestroy() {
        signalHandler.removeCallbacks(adminSignalPoll)
        signalHandler.removeCallbacks(resumeService)
        signalHandler.removeCallbacks(hotspotNoClientWatchdog)
        signalHandler.removeCallbacks(webUiCountdown)
        if (serviceBound) {
            boundService?.unregisterClient(bleListener)
            unbindService(serviceConnection)
            serviceBound = false
        }
        if (settings.autoOpenEnabled) startAutoOpenService()
        clearPhoneBeaconHotspotIp()
        localHotspotReservation?.close()
        localHotspotReservation = null
        hotspotSessionId = null
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
            if (hasBlePermissions()) {
                if (webUiBlePermissionPending) {
                    webUiBlePermissionPending = false
                    beginPhoneHotspotWebUi()
                } else if (autoOpenPermissionPending) {
                    autoOpenPermissionPending = false
                    setAutomaticBeaconEnabled(true)
                } else if (settings.autoOpenEnabled) {
                    startAutoOpenService()
                }
            }
        } else if (requestCode == REQUEST_HOTSPOT_PERMISSIONS) {
            if (hotspotPermissionsGranted() && webUiRequestPending && hotspotProvisioningStarted) {
                startLocalHotspot()
            } else if (!hotspotPermissionsGranted()) {
                webUiRequestPending = false
                hotspotProvisioningStarted = false
                finishWebUiIpRequest()
                appendLog("Нет разрешения на создание локальной точки доступа")
            }
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

        // Main controls intentionally resemble a simple industrial pendant:
        // large, well-separated tiles that are easy to hit with one hand.
        val accessCard = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
        }
        manualOpenButton = controlTile(R.string.mode_auto) { toggleAutomaticMode() }
        accessCard.addView(manualOpenButton, controlTileParams(heightDp = 118))
        manualKeepOpenButton = controlTile(R.string.mode_always_open) { toggleKeepOpenMode() }
        accessCard.addView(manualKeepOpenButton, controlTileParams(heightDp = 118, topMarginDp = 18))
        val internetOpenButton = controlTile(R.string.open_via_internet) { }
        internetOpenButton.isEnabled = false
        internetOpenButton.alpha = 0.5f
        accessCard.addView(internetOpenButton, controlTileParams(heightDp = 118, topMarginDp = 18))

        hotspotOpenButton = controlTile(R.string.control_settings) { openEspWebUi() }
        hotspotOpenButton.isEnabled = true
        hotspotOpenButton.visibility = View.GONE
        accessCard.addView(hotspotOpenButton, controlTileParams(heightDp = 118, topMarginDp = 18))
        root.addView(accessCard, spacedFullWidth().apply { bottomMargin = dp(48) })

        // --- Admin container: everything that only appears once the lock exposes admin mode ---
        adminContainer = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            visibility = View.GONE
        }
        root.addView(adminContainer, fullWidth())

        hotspotStatusView = TextView(this).apply {
            text = getString(R.string.local_hotspot_status)
            textSize = 14f
            setTextColor(getColor(R.color.secondary_text))
        }
        adminContainer.addView(hotspotStatusView, topSpacedFullWidth(10))
        hotspotWebUrlView = TextView(this).apply {
            textSize = 14f
            setTextColor(getColor(R.color.accent_blue))
            visibility = View.GONE
        }
        adminContainer.addView(hotspotWebUrlView, topSpacedFullWidth(6))
        dynamicSettingsContainer = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            visibility = View.GONE
        }
        staticSettingsContainer = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
        }

        // --- RSSI ---
        staticSettingsContainer.addView(sectionHeader(R.string.section_rssi), topSpacedFullWidth(12))
        val rssiCard = newCard()
        espOpenRssiField = adminNumberField(true)
        espHoldRssiField = adminNumberField(true)
        addRow(rssiCard, stepperRow(getString(R.string.esp_open_rssi_threshold), espOpenRssiField, -100, 0, 1))
        addRow(rssiCard, stepperRow(getString(R.string.esp_hold_rssi_threshold), espHoldRssiField, -100, 0, 1))
        staticSettingsContainer.addView(rssiCard, fullWidth())

        // --- ТАЙМЕРЫ ---
        staticSettingsContainer.addView(sectionHeader(R.string.section_timers), topSpacedFullWidth(20))
        val timersCard = newCard()
        adminTimerA = adminNumberField(false)
        addRow(timersCard, stepperRow(getString(R.string.admin_timer_a), adminTimerA, 0, 10_000, 100))
        staticSettingsContainer.addView(timersCard, fullWidth())

        // --- АППАРАТНАЯ ЧАСТЬ ---
        staticSettingsContainer.addView(sectionHeader(R.string.section_hardware), topSpacedFullWidth(20))
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
        legacyAdvPowerContainer = newCard()
        addRow(legacyAdvPowerContainer, disclosureRow(
            getString(R.string.admin_adv_tx_power), adminAdvTxPowerValue, adminAdvTxPower
        ))

        // --- WI-FI OTA ---
        
        val otaCard = newCard()
        otaSsidField = otaTextField(false)
        otaPasswordField = otaTextField(true)
        otaUrlField = otaTextField(false)
        addRow(otaCard, textFieldRow(getString(R.string.ota_wifi_ssid), otaSsidField))
        addRow(otaCard, textFieldRow(getString(R.string.ota_wifi_password), otaPasswordField))
        addRow(otaCard, textFieldRow(getString(R.string.ota_url), otaUrlField))
        addRow(otaCard, filledButton(R.string.ota_read) { repository.readOtaConfig() })
        addRow(otaCard, filledButton(R.string.ota_save) { saveOtaConfig() })
        addRow(otaCard, filledButton(R.string.ota_start) { startOtaUpdate() })
        legacyOtaContainer = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(otaCard, fullWidth())
        }

        // The ESP32 settings and actions are intentionally not attached to the
        // phone service panel; they are available in the ESP32 Web UI.
        adminSaveButton = filledButton(R.string.admin_save) {
            if (syncDynamicSettings()) saveAdminConfig()
        }
        adminSaveButton.isEnabled = false

        adminBondsContainer = newCard()
        freeKeysView = TextView(this).apply {
            textSize = 13f
            setTextColor(getColor(R.color.secondary_text))
        }
        val bondActionsCard = newCard()
        adminDeleteBondButton = destructiveButton(R.string.admin_delete_bond) {
            selectedBondAddress?.let { repository.deleteBond(it) }
        }
        adminDeleteBondButton.isEnabled = false
        addRow(bondActionsCard, adminDeleteBondButton)
        val dangerCard = newCard()
        addRow(dangerCard, addAdminButton(R.string.admin_delete_all) {
            appendLog("После удаления всех bonds удалите BLE Lock в настройках Bluetooth телефона")
            repository.deleteAllBonds()
        })
        addRow(dangerCard, addAdminButton(R.string.admin_reboot) {
            appendLog("Команда перезагрузки ESP32")
            repository.rebootEsp32()
        })

        privacyPolicyButton = TextView(this).apply {
            setText(R.string.privacy_policy_button)
            textSize = 14f
            gravity = Gravity.CENTER
            setTextColor(getColor(R.color.accent_blue))
            paintFlags = paintFlags or android.graphics.Paint.UNDERLINE_TEXT_FLAG
            setPadding(dp(8), dp(12), dp(8), dp(12))
            isClickable = true
            isFocusable = true
            setOnClickListener { showPrivacyPolicy() }
        }
        adminContainer.addView(Space(this), fullWidth().apply { height = dp(32) })

        val scroll = ScrollView(this).apply {
            isFillViewport = true
            setBackgroundColor(Color.TRANSPARENT)
            addView(root)
        }
        swipeRefresh = SwipeRefreshLayout(this).apply {
            setColorSchemeResources(R.color.accent_blue)
            isEnabled = false
            setOnRefreshListener {
                if (adminModeAvailable) {
                    repository.pollAdminState()
                    signalHandler.postDelayed({ isRefreshing = false }, SWIPE_REFRESH_TIMEOUT_MS)
                } else {
                    isRefreshing = false
                }
            }
            addView(scroll, ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
        }
        return FrameLayout(this).apply {
            setBackgroundColor(getColor(R.color.screen_background))
            setOnApplyWindowInsetsListener { view, insets ->
                val topInset: Int
                val bottomInset: Int
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                    val systemBars = insets.getInsets(WindowInsets.Type.systemBars())
                    topInset = systemBars.top
                    bottomInset = systemBars.bottom
                } else {
                    @Suppress("DEPRECATION")
                    topInset = insets.systemWindowInsetTop
                    @Suppress("DEPRECATION")
                    bottomInset = insets.systemWindowInsetBottom
                }
                view.setPadding(0, topInset, 0, bottomInset)
                insets
            }
            addView(swipeRefresh, FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            ).apply { bottomMargin = dp(84) })
            val appInfo = LinearLayout(this@MainActivity).apply {
                orientation = LinearLayout.VERTICAL
                gravity = Gravity.CENTER
                setPadding(dp(8), dp(4), dp(8), 0)
                addView(TextView(this@MainActivity).apply {
                    text = getString(
                        R.string.app_version_release,
                        BuildConfig.BUILD_NUMBER
                    )
                    textSize = 12f
                    gravity = Gravity.CENTER
                    setTextColor(getColor(R.color.secondary_text))
                }, fullWidth())
                addView(TextView(this@MainActivity).apply {
                    text = getString(R.string.app_version_timestamp, BuildConfig.BUILD_TIMESTAMP)
                    textSize = 12f
                    gravity = Gravity.CENTER
                    setTextColor(getColor(R.color.secondary_text))
                }, fullWidth())
                addView(privacyPolicyButton, fullWidth())
            }
            addView(appInfo, FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.BOTTOM
            ))
            post { requestApplyInsets() }
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

    private fun createBeaconPowerScreen(): View {
        val powerLabels = resources.getStringArray(R.array.phone_beacon_tx_power_levels)
        val valueLabel = TextView(this).apply {
            text = powerLabels[settings.phoneBeaconTxPowerLevel]
            textSize = 32f
            setTypeface(typeface, Typeface.BOLD)
            gravity = Gravity.CENTER
            setTextColor(getColor(R.color.accent_blue))
        }
        val slider = VerticalLevelSlider(
            this,
            LockSettings.MIN_PHONE_BEACON_TX_POWER_LEVEL,
            LockSettings.MAX_PHONE_BEACON_TX_POWER_LEVEL,
            getString(R.string.phone_beacon_tx_power_title)
        ).apply {
            setValue(settings.phoneBeaconTxPowerLevel, notify = false)
            onValueChanged = { value ->
                settings.phoneBeaconTxPowerLevel = value
                valueLabel.text = powerLabels[value]
                startService(Intent(this@MainActivity, BleLockService::class.java)
                    .setAction(BleLockService.ACTION_BEACON_TX_POWER_CHANGED))
            }
        }
        beaconPowerSlider = slider

        val scaleLabels = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            addView(TextView(this@MainActivity).apply {
                text = powerLabels[LockSettings.MAX_PHONE_BEACON_TX_POWER_LEVEL]
                setTextColor(getColor(R.color.secondary_text))
            })
            addView(Space(this@MainActivity), LinearLayout.LayoutParams(1, 0, 1f))
            addView(TextView(this@MainActivity).apply {
                text = powerLabels[LockSettings.MIN_PHONE_BEACON_TX_POWER_LEVEL]
                setTextColor(getColor(R.color.secondary_text))
            })
        }

        val sliderRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER
            addView(scaleLabels, LinearLayout.LayoutParams(dp(80), ViewGroup.LayoutParams.MATCH_PARENT))
            addView(slider, LinearLayout.LayoutParams(dp(64), ViewGroup.LayoutParams.MATCH_PARENT))
        }

        val screen = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(20), dp(12), dp(20), dp(24))
            setBackgroundColor(getColor(R.color.screen_background))
        }
        screen.addView(TextView(this).apply {
            text = getString(R.string.phone_beacon_tx_power_title)
            textSize = 23f
            setTypeface(typeface, Typeface.BOLD)
            gravity = Gravity.CENTER
            setTextColor(getColor(R.color.primary_text))
        }, topSpacedFullWidth(20))
        screen.addView(valueLabel, topSpacedFullWidth(12))
        screen.addView(sliderRow, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            0,
            1f
        ).apply {
            topMargin = dp(20)
            bottomMargin = dp(20)
        })
        screen.addView(TextView(this).apply {
            text = getString(R.string.phone_beacon_tx_power_description)
            textSize = 14f
            setTextColor(getColor(R.color.secondary_text))
            setPadding(dp(8), dp(12), dp(8), dp(12))
        }, fullWidth())
        return screen
    }

    private fun isInsideBeaconPowerSlider(rawX: Float, rawY: Float): Boolean {
        val slider = beaconPowerSlider ?: return false
        val location = IntArray(2)
        slider.getLocationOnScreen(location)
        return rawX >= location[0] && rawX <= location[0] + slider.width &&
            rawY >= location[1] && rawY <= location[1] + slider.height
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
        if (serviceBound) lastBleState = repository.currentState
        permissionButton.visibility = if (permissionsGranted) View.GONE else View.VISIBLE
        hotspotOpenButton.visibility = if (permissionsGranted) View.VISIBLE else View.GONE
        updateAdminEntryState()
        updateMainControlsEnabled()
        updateAdminButtonsEnabled()
    }

    private fun showLegacyBleLocationNoticeIfNeeded() {
        if (Build.VERSION.SDK_INT !in Build.VERSION_CODES.M..Build.VERSION_CODES.R) return
        val locationManager = getSystemService(LocationManager::class.java)
        val locationEnabled = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            locationManager.isLocationEnabled
        } else {
            @Suppress("DEPRECATION")
            Settings.Secure.getInt(
                contentResolver,
                Settings.Secure.LOCATION_MODE,
                Settings.Secure.LOCATION_MODE_OFF
            ) != Settings.Secure.LOCATION_MODE_OFF
        }
        if (locationEnabled || legacyLocationDialog?.isShowing == true) return

        legacyLocationDialog = AlertDialog.Builder(this)
            .setTitle(R.string.legacy_location_required_title)
            .setMessage(R.string.legacy_location_required_message)
            .setPositiveButton(R.string.legacy_location_open_settings) { _, _ ->
                startActivity(Intent(Settings.ACTION_LOCATION_SOURCE_SETTINGS))
            }
            .setNegativeButton(android.R.string.cancel, null)
            .setOnDismissListener { legacyLocationDialog = null }
            .show()
    }

    private fun updateMainControlsEnabled() {
        if (!::manualOpenButton.isInitialized || !::manualKeepOpenButton.isInitialized) return
        renderWorkingMode()
        if (::hotspotOpenButton.isInitialized) {
            val settingsEnabled = settings.ownerAccessConfirmed
            hotspotOpenButton.visibility = if (settingsEnabled) View.VISIBLE else View.GONE
            hotspotOpenButton.isEnabled = settingsEnabled
            hotspotOpenButton.alpha = if (settingsEnabled) 1f else 0.38f
        }
    }

    private fun controlTile(textRes: Int, onClick: () -> Unit): Button =
        outlineButton(textRes, onClick).apply {
            textSize = 20f
            minHeight = 0
            minWidth = 0
            gravity = Gravity.CENTER
            maxLines = 3
            elevation = dp(5).toFloat()
            setPadding(dp(20), dp(20), dp(20), dp(20))
        }

    private fun controlTileParams(heightDp: Int, topMarginDp: Int = 0) =
        LinearLayout.LayoutParams(dp(252), dp(heightDp)).apply {
            gravity = Gravity.CENTER_HORIZONTAL
            topMargin = dp(topMarginDp)
        }

    private fun updateAdminEntryState() {
        if (::hotspotOpenButton.isInitialized) {
            val ownerReady = settings.ownerAccessConfirmed
            hotspotOpenButton.visibility = if (ownerReady) View.VISIBLE else View.GONE
            hotspotOpenButton.isEnabled = ownerReady
            hotspotOpenButton.alpha = if (ownerReady) 1f else 0.38f
        }
        if (::swipeRefresh.isInitialized) {
            swipeRefresh.isEnabled = adminModeAvailable && serviceMenuRequested
        }
        updateMainControlsEnabled()
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

    private fun bindBleService() {
        if (serviceBound) return
        val intent = Intent(this, BleLockService::class.java)
        bindService(intent, serviceConnection, Context.BIND_AUTO_CREATE)
    }

    private fun startServiceConnection() {
        // The foreground service remains the only BLE owner. Admin mode is
        // requested through its Binder instead of creating an Activity GATT.
        manualConnectionActive = true
        adminProbeRetries = 0
        signalHandler.removeCallbacks(resumeService)
        if (hasBlePermissions()) {
            appendLog("Подключение к замку…")
            signalHandler.postDelayed({ boundService?.requestAdminConnection() }, MANUAL_CONNECT_DELAY_MS)
        } else {
            requestMissingPermissions()
        }
    }

    private fun activateServiceMenu() {
        serviceMenuRequested = true
        updateAdminEntryState()
        if (adminModeAvailable && repository.currentState == BleRepository.State.READY) {
            setAdminPanelVisible(true)
            signalHandler.removeCallbacks(adminSignalPoll)
            signalHandler.postDelayed(adminSignalPoll, ADMIN_SIGNAL_POLL_INTERVAL_MS)
            updateAdminButtonsEnabled()
            repository.readAdminConfig()
        } else {
            adminModeAvailable = false
            setAdminPanelVisible(false)
            signalHandler.removeCallbacks(adminSignalPoll)
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
                openBatteryOptimizationSettings()
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

    private fun openBatteryOptimizationSettings() {
    val intent = Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS)
    runCatching { startActivity(intent) }
        .onFailure { startActivity(Intent(Settings.ACTION_SETTINGS)) }
}

    private fun stopAutoOpenService() {
        stopService(Intent(this, BleLockService::class.java))
    }

    private fun appendLog(message: String) {
        android.util.Log.d("BleLockUi", message)
    }

    private fun adminNumberField(signed: Boolean = false) = EditText(this).apply {
        isSingleLine = true
        inputType = InputType.TYPE_CLASS_NUMBER or
            if (signed) InputType.TYPE_NUMBER_FLAG_SIGNED else 0
    }
    private val otaStatusPoll = object : Runnable {
        override fun run() {
            if (otaStartPending || runtimeWebPending || webUiRequestPending) {
                if (!repository.readOtaStatus()) {
                    if (otaStartPending) otaStartPending = false
                    if (webUiRequestPending) {
                        signalHandler.postDelayed(this, 1000L)
                        return
                    }
                    if (runtimeWebPending) runtimeWebPending = false
                    signalHandler.removeCallbacks(this)
                    return
                }
                signalHandler.postDelayed(this, 1000L)
            }
        }
    }

    private fun showPrivacyPolicy() {
        startActivity(
            Intent(
                Intent.ACTION_VIEW,
                Uri.parse(getString(R.string.privacy_policy_url))
            )
        )
    }

    private fun sendOpenCommand(keepOpen: Boolean) {
        manualOpenCommandPending = !keepOpen
        keepOpenCommandPending = keepOpen
        keepOpenStatusExpected = false
        resetManualButtonStates()
        if (serviceBound && repository.currentState == BleRepository.State.READY) {
            manualCommandWaitingForReady = false
            if (keepOpen) repository.toggleKeepOpen() else repository.forceOpenNow()
            return
        }
        manualCommandWaitingForReady = true
        appendLog("Запускаю BLE-подключение для ручной команды")
        startServiceConnection()
    }

    private fun toggleAutomaticMode() {
        if (!hasBlePermissions()) {
            autoOpenPermissionPending = true
            requestMissingPermissions()
            return
        }
        setAutomaticBeaconEnabled(!settings.autoOpenEnabled)
    }

    private fun setAutomaticBeaconEnabled(enabled: Boolean) {
        settings.autoOpenEnabled = enabled
        workingModeState = WorkingModeReducer.reduce(
            workingModeState, WorkingModeEvent.AutoOpenChanged(enabled)
        )
        if (enabled) {
            startAutoOpenService()
            if (!serviceBound) bindBleService() else boundService?.resumeAutomaticMode()
        } else if (serviceBound) {
            boundService?.setAutomaticMode(false)
        } else {
            startService(Intent(this, BleLockService::class.java).setAction(BleLockService.ACTION_STOP))
        }
        appendLog(if (enabled) "Автооткрытие включено: BLE-маяк телефона активен" else "Автооткрытие выключено: BLE-маяк телефона остановлен")
        renderWorkingMode()
    }

    private fun toggleKeepOpenMode() {
        if (BleLockService.lastHoldCommandPending) return
        BleLockService.lastHoldCommandPending = true
        try {
            ContextCompat.startForegroundService(this, Intent(this, BleLockService::class.java)
                .setAction(BleLockService.ACTION_HOLD_OPEN_TOGGLE))
            appendLog("Запрошено переключение постоянного открытия; ожидаю подтверждение ESP32 по BLE")
        } catch (error: Exception) {
            BleLockService.lastHoldCommandPending = false
            Toast.makeText(this, "Не удалось отправить команду замку", Toast.LENGTH_LONG).show()
            appendLog("Не удалось запустить BLE-команду: ${error.message}")
        }
        renderWorkingMode()
    }

    private fun resetManualButtonStates() = renderWorkingMode()

    private fun renderWorkingMode() {
        if (!::manualOpenButton.isInitialized || !::manualKeepOpenButton.isInitialized) return
        val bluetoothReady = getSystemService(BluetoothManager::class.java).adapter?.isEnabled == true
        val ui = WorkingModeUi.compute(workingModeState,
            bluetoothReady && settings.ownerAccessConfirmed && !BleLockService.lastHoldCommandPending)
        manualKeepOpenButton.text = if (BleLockService.lastHoldCommandPending) "Ожидание ESP32…"
            else getString(R.string.mode_always_open)
        setManualButtonBlue(manualOpenButton, ui.autoSelected)
        setManualButtonBlue(manualKeepOpenButton, ui.alwaysOpenSelected)
        manualOpenButton.isEnabled = ui.autoEnabled
        manualKeepOpenButton.isEnabled = ui.alwaysOpenEnabled
        manualOpenButton.alpha = if (ui.autoEnabled) 1f else 0.38f
        manualKeepOpenButton.alpha = if (ui.alwaysOpenEnabled) 1f else 0.38f
    }

    private fun setManualButtonBlue(button: Button, blue: Boolean) {
        button.isSelected = blue
        button.background = getDrawable(
            if (blue) R.drawable.bg_button_filled else R.drawable.bg_button_outline
        )
        button.setTextColor(if (blue) Color.WHITE else getColor(R.color.accent_blue))
    }

    private fun setAdminPanelVisible(visible: Boolean) {
        adminContainer.visibility = if (visible) View.VISIBLE else View.GONE
        if (::privacyPolicyButton.isInitialized) {
            privacyPolicyButton.visibility = if (visible) View.GONE else View.VISIBLE
        }
    }

    private fun closeServiceMenu() {
        serviceMenuRequested = false
        manualConnectionActive = false
        adminModeAvailable = false
        updateAdminEntryState()
        adminProbeRetries = 0
        signalHandler.removeCallbacks(adminSignalPoll)
        signalHandler.removeCallbacks(resumeService)
        setAdminPanelVisible(false)
        if (serviceBound) {
            if (settings.autoOpenEnabled) {
                boundService?.resumeAutomaticMode()
            } else {
                boundService?.setAutomaticMode(false)
            }
        }
        appendLog("Сервисные настройки закрыты")
        refreshState()
    }
    private val serviceConnection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
            val binder = service as? BleLockService.LocalBinder ?: return
            boundService = binder
            repository = binder
            serviceBound = true
            binder.registerClient(bleListener)
            workingModeState = workingModeState.copy(autoOpenEnabled = settings.autoOpenEnabled)
            refreshState()
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            serviceBound = false
            boundService = null
            adminModeAvailable = false
            updateAdminEntryState()
            setAdminPanelVisible(false)
        }
    }

    private fun otaTextField(password: Boolean) = EditText(this).apply {
        isSingleLine = true
        inputType = InputType.TYPE_CLASS_TEXT or
            if (password) InputType.TYPE_TEXT_VARIATION_PASSWORD else 0
        minHeight = dp(48)
        setPadding(dp(12), 0, dp(12), 0)
        setTextColor(getColor(R.color.primary_text))
        setHintTextColor(getColor(R.color.secondary_text))
        background = getDrawable(android.R.drawable.edit_text)
        isFocusable = true
        isFocusableInTouchMode = true
    }

    private fun textFieldRow(label: String, field: EditText): View =
        LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(4), dp(16), dp(4))
            field.hint = label
            addView(field, fullWidth())
        }

    private fun startOtaUpdate() {
        appendLog("Запуск OTA через домашнюю Wi-Fi сеть")
        otaStartPending = true
        otaStartToastShown = false
        signalHandler.removeCallbacks(otaStatusPoll)
        signalHandler.postDelayed(otaStatusPoll, 1000L)
        repository.startHomeOta()
    }

    private fun saveOtaConfig() {
        val ssid = otaSsidField.text.toString().trim()
        val password = otaPasswordField.text.toString()
        val url = otaUrlField.text.toString().trim()
        if (ssid.isEmpty() || !url.startsWith("https://")) {
            appendLog("Укажите SSID и HTTPS URL прошивки")
            return
        }
        repository.writeOtaConfig("${BleProtocol.OTA_PERSIST_PREFIX}$ssid\n$password\n$url")
    }

    private fun renderDeclarativeSettings(schema: DeclarativeUiSchema) {
        dynamicInputs.clear()
        dynamicSchemaItems.clear()
        dynamicSettingsContainer.removeAllViews()
        dynamicSettingsContainer.visibility = View.VISIBLE
        staticSettingsContainer.visibility = View.GONE
        legacyAdvPowerContainer.visibility = View.VISIBLE
        legacyOtaContainer.visibility = View.GONE

        dynamicSettingsContainer.addView(TextView(this).apply {
            text = "${schema.title} (декларативная панель)"
            textSize = 18f
            setTypeface(typeface, Typeface.BOLD)
            setTextColor(getColor(R.color.primary_text))
        }, topSpacedFullWidth(16))

        schema.sections.forEach { section ->
            dynamicSettingsContainer.addView(
                TextView(this).apply {
                    text = section.title
                    textSize = 16f
                    setTypeface(typeface, Typeface.BOLD)
                    setTextColor(getColor(R.color.primary_text))
                }, topSpacedFullWidth(12)
            )
            val card = newCard()
            section.items.forEach { item ->
                dynamicSchemaItems[item.id] = item
                val row = when (item.uiType) {
                    "slider" -> {
                        val field = otaTextField(item.readOnly).apply {
                            inputType = InputType.TYPE_CLASS_NUMBER or
                                InputType.TYPE_NUMBER_FLAG_SIGNED or
                                InputType.TYPE_NUMBER_FLAG_DECIMAL
                            setText(item.value.asLongOrNull()?.toString().orEmpty())
                            isEnabled = !item.readOnly
                        }
                        dynamicInputs[item.id] = field
                        textFieldRow(item.title, field)
                    }
                    "switch" -> {
                        val control = Switch(this).apply {
                            isChecked = item.value as? Boolean ?: false
                            isEnabled = !item.readOnly
                        }
                        dynamicInputs[item.id] = control
                        switchRow(item.title, control)
                    }
                    "text" -> {
                        val field = otaTextField(item.id == "ota_wifi_password").apply {
                            setText(item.value?.toString().orEmpty())
                            isEnabled = !item.readOnly
                        }
                        dynamicInputs[item.id] = field
                        textFieldRow(item.title, field)
                    }
                    "info" -> valueRow(
                        item.title,
                        TextView(this).apply {
                            text = item.value?.toString().orEmpty() +
                                if (item.unit.isBlank()) "" else " ${item.unit}"
                            textSize = 16f
                            setTextColor(getColor(R.color.secondary_text))
                        }
                    )
                    "button" -> when (item.id) {
                        "ota_read" -> filledButton(R.string.ota_read) { repository.readOtaConfig() }
                        "ota_save" -> filledButton(R.string.ota_save) {
                            if (syncDynamicSettings(validateOta = true)) saveOtaConfig()
                        }
                        "ota_start" -> filledButton(R.string.ota_start) { startOtaUpdate() }
                        else -> null
                    }
                    else -> null
                }
                if (row != null) addRow(card, row)
            }
            dynamicSettingsContainer.addView(card, fullWidth())
        }
        appendLog("Динамический renderer построен из схемы")
    }

    private fun syncDynamicSettings(validateOta: Boolean = false): Boolean {
        if (dynamicInputs.isEmpty()) return true
        fun read(id: String): Long? = (dynamicInputs[id] as? EditText)?.text?.toString()?.toLongOrNull()
        val values = mutableMapOf<String, Long>()
        var valid = true
        dynamicSchemaItems.values
            .filter { it.uiType == "slider" }
            .forEach { item ->
                val value = read(item.id)
                val min = item.min
                val max = item.max
                val step = item.step
                if (value == null || min == null || max == null || step == null ||
                    value !in min..max || (value - min) % step != 0L) {
                    valid = false
                }
            }
        val ssid = (dynamicInputs["ota_wifi_ssid"] as? EditText)?.text?.toString()?.trim()
        val password = (dynamicInputs["ota_wifi_password"] as? EditText)?.text?.toString()
        val url = (dynamicInputs["ota_firmware_url"] as? EditText)?.text?.toString()?.trim()
        if (validateOta && ssid != null &&
            (ssid.isEmpty() || !url.orEmpty().startsWith("https://"))) {
            valid = false
        }
        if (!valid) {
            Toast.makeText(this, "Проверьте диапазоны и OTA-поля", Toast.LENGTH_LONG).show()
            return false
        }
        val mapping = listOf(
            "phone_rssi_threshold" to { value: Long -> espOpenRssiField.setText(value.toString()) },
            "timer_a_ms" to { value: Long -> adminTimerA.setText(value.toString()) }
        )
        mapping.forEach { (id, apply) ->
            read(id)?.let { values[id] = it; apply(it) }
        }
        (dynamicInputs["ota_wifi_ssid"] as? EditText)?.let { otaSsidField.setText(it.text.toString()) }
        (dynamicInputs["ota_wifi_password"] as? EditText)?.let { otaPasswordField.setText(it.text.toString()) }
        (dynamicInputs["ota_firmware_url"] as? EditText)?.let { otaUrlField.setText(it.text.toString()) }
        if (values.size < mapping.size) {
            Toast.makeText(this, "Проверьте числовые значения декларативной панели", Toast.LENGTH_LONG).show()
            return false
        }
        return true
    }

    private fun saveAdminConfig() {
        val values = listOf(
            espOpenRssiField.text.toString(), espHoldRssiField.text.toString(),
            "0", adminTimerA.text.toString(), "0"
        )
        if (values.any { it.toLongOrNull() == null }) {
            appendLog("Заполните все числовые поля настроек")
            return
        }
        val advTxPower = ADV_TX_POWER_VALUES.getOrElse(adminAdvTxPower.selectedItemPosition) {
            DEFAULT_ADV_TX_POWER_DBM
        }
        val config = values.joinToString(",") + ",$advTxPower"
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

    private fun openEspWebUi() {
        if (!settings.ownerAccessConfirmed || hotspotCredentialWriteInFlight) return
        if (!hasBlePermissions()) {
            webUiBlePermissionPending = true
            requestMissingPermissions()
            return
        }
        beginPhoneHotspotWebUi()
    }

    private fun beginPhoneHotspotWebUi() {
        webUiRequestPending = true
        hotspotProvisioningStarted = true
        runtimeWebPending = false
        webUiStartedAtMs = SystemClock.elapsedRealtime()
        webUiCountdownStartSeconds = settings.webUiCountdownSeconds
        setWebUiConnectionState(WebUiConnectionState.STARTING_HOTSPOT)
        signalHandler.removeCallbacks(webUiCountdown)
        signalHandler.post(webUiCountdown)
        runCatching {
            startService(
                Intent(this, BleLockService::class.java)
                    .setAction(BleLockService.ACTION_WEB_UI_IP_REQUEST)
            )
        }.onFailure {
            webUiRequestPending = false
            signalHandler.removeCallbacks(webUiCountdown)
            setWebUiConnectionState(WebUiConnectionState.ERROR)
            appendLog("Не удалось запросить IP ESP32 по BLE: ${it.message}")
            return
        }
        appendLog("Создаю точку доступа телефона для Web UI и передаю её параметры по encrypted BLE")
        startWebUiProvisioning()
    }

    private fun openWebUiAt(ip: String) {
        if (!ip.isValidIpv4()) return
        val hadPendingRequest = webUiRequestPending
        signalHandler.removeCallbacks(webUiCountdown)
        if (hadPendingRequest) {
            val elapsedMs = SystemClock.elapsedRealtime() - webUiStartedAtMs
            settings.webUiCountdownSeconds =
                ((elapsedMs + 999L) / 1000L).toInt().coerceIn(1, HOTSPOT_PROVISIONING_TIMEOUT_SECONDS)
        }
        settings.lockWifiIp = ip
        val url = "http://$ip/"
        hotspotWebUrlView.text = url
        hotspotWebUrlView.visibility = View.VISIBLE
        hotspotWebUrlView.tag = url
        if (localHotspotReservation != null && !runtimeWebPending) {
            closeLocalHotspot("ESP32 сообщила доступный IP существующей Wi-Fi сети")
        } else if (localHotspotReservation == null && hotspotSessionId != null) {
            clearPhoneBeaconHotspotIp(hotspotSessionId)
            hotspotSessionId = null
        }
        webUiRequestPending = false
        hotspotProvisioningStarted = false
        val keepHotspotForEsp = localHotspotReservation != null && runtimeWebPending
        if (keepHotspotForEsp) {
            hotspotExpectedEspIp = ip
            hotspotOpenedEspIp = ip
        }
        webUiBrowserOpen = hadPendingRequest || keepHotspotForEsp
        setWebUiConnectionState(WebUiConnectionState.OPENING_BROWSER)
        appendLog("Открываю Web UI ESP32: $url")
        runCatching { startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url))) }
            .onSuccess {
                setWebUiConnectionState(WebUiConnectionState.COMPLETE)
                if (keepHotspotForEsp) {
                    signalHandler.removeCallbacks(hotspotNoClientWatchdog)
                    signalHandler.postDelayed(hotspotNoClientWatchdog, HOTSPOT_IP_CHECK_INTERVAL_MS)
                }
            }
            .onFailure {
                webUiBrowserOpen = false
                setWebUiConnectionState(WebUiConnectionState.ERROR)
                appendLog("Не удалось открыть браузер: ${it.message}")
                finishWebUiIpRequest()
            }
    }

    private fun finishWebUiIpRequest() {
        signalHandler.removeCallbacks(webUiCountdown)
        runCatching {
            startService(
                Intent(this, BleLockService::class.java)
                    .setAction(BleLockService.ACTION_WEB_UI_IP_FINISHED)
            )
        }
    }

    private fun startWebUiProvisioning() {
        if (localHotspotReservation != null) {
            resolveHotspotIpv4ThenSend(localHotspotReservation!!, hotspotSessionId)
            return
        }
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) {
            webUiRequestPending = false
            hotspotProvisioningStarted = false
            setWebUiConnectionState(WebUiConnectionState.ERROR)
            finishWebUiIpRequest()
            appendLog("Эта версия Android не умеет создавать локальную точку доступа")
            return
        }
        if (!hotspotPermissionsGranted()) {
            val permissions = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                arrayOf(Manifest.permission.NEARBY_WIFI_DEVICES)
            } else {
                arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
            }
            requestPermissions(permissions, REQUEST_HOTSPOT_PERMISSIONS)
            return
        }
        setWebUiConnectionState(WebUiConnectionState.STARTING_HOTSPOT)
        startLocalHotspot()
    }

    @Suppress("DEPRECATION")
    private fun startLocalHotspot() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return
        val wifiManager = getSystemService(Context.WIFI_SERVICE) as WifiManager
        val requestSessionId = java.util.UUID.randomUUID().toString()
        hotspotSessionId = requestSessionId
        hotspotStatusView.setText(R.string.local_hotspot_status)
        runCatching {
            startService(
                Intent(this, BleLockService::class.java)
                    .setAction(BleLockService.ACTION_WIFI_PROVISION_PENDING)
                    .putExtra(BleLockService.EXTRA_HOTSPOT_SESSION, requestSessionId)
            )
        }.onFailure {
            hotspotSessionId = null
            setWebUiConnectionState(WebUiConnectionState.ERROR)
            finishWebUiIpRequest()
            appendLog("Не удалось приостановить AUTO-маяк для настройки Wi-Fi: ${it.message}")
            return
        }
        try {
            wifiManager.startLocalOnlyHotspot(object : WifiManager.LocalOnlyHotspotCallback() {
            override fun onStarted(reservation: WifiManager.LocalOnlyHotspotReservation) {
                if (hotspotSessionId != requestSessionId) {
                    reservation.close()
                    return
                }
                localHotspotReservation = reservation
                hotspotExpectedEspIp = null
                hotspotOpenedEspIp = null
                hotspotIpCheckPending = false
                signalHandler.removeCallbacks(hotspotNoClientWatchdog)
                signalHandler.postDelayed(hotspotNoClientWatchdog, HOTSPOT_PROVISIONING_TIMEOUT_MS)
                hotspotWebUrlView.text = ""
                hotspotWebUrlView.tag = null
                hotspotWebUrlView.visibility = View.GONE
                hotspotOpenButton.isEnabled = true
                hotspotOpenButton.visibility = View.VISIBLE
                hotspotStatusView.text = "Локальная сеть создана"
                appendLog("Локальная точка доступа создана")
                hotspotStatusView.text = "Точка доступа готова, определяю IPv4 телефона…"
                resolveHotspotIpv4ThenSend(reservation, requestSessionId)
            }

            override fun onStopped() {
                clearPhoneBeaconHotspotIp(requestSessionId)
                if (hotspotSessionId != requestSessionId) return
                localHotspotReservation = null
                hotspotSessionId = null
                hotspotCredentialWriteInFlight = false
                pendingHotspotWifi = null
                hotspotExpectedEspIp = null
                hotspotOpenedEspIp = null
                hotspotIpCheckPending = false
                signalHandler.removeCallbacks(hotspotNoClientWatchdog)
                if (webUiRequestPending) {
                    webUiRequestPending = false
                    hotspotProvisioningStarted = false
                    setWebUiConnectionState(WebUiConnectionState.ERROR)
                    finishWebUiIpRequest()
                }
                hotspotStatusView.setText(R.string.local_hotspot_status)
                hotspotWebUrlView.text = ""
                hotspotWebUrlView.tag = null
                hotspotWebUrlView.visibility = View.GONE
                hotspotOpenButton.isEnabled = true
                hotspotOpenButton.visibility = View.VISIBLE
                appendLog("Локальная точка доступа остановлена системой")
            }

            override fun onFailed(reason: Int) {
                clearPhoneBeaconHotspotIp(requestSessionId)
                if (hotspotSessionId != requestSessionId) return
                localHotspotReservation = null
                hotspotSessionId = null
                hotspotCredentialWriteInFlight = false
                pendingHotspotWifi = null
                hotspotExpectedEspIp = null
                hotspotOpenedEspIp = null
                hotspotIpCheckPending = false
                signalHandler.removeCallbacks(hotspotNoClientWatchdog)
                webUiRequestPending = false
                hotspotProvisioningStarted = false
                setWebUiConnectionState(WebUiConnectionState.ERROR)
                finishWebUiIpRequest()
                hotspotStatusView.text = "Не удалось создать сеть, код Android: $reason"
                hotspotWebUrlView.text = ""
                hotspotWebUrlView.tag = null
                hotspotWebUrlView.visibility = View.GONE
                hotspotOpenButton.isEnabled = true
                hotspotOpenButton.visibility = View.VISIBLE
                appendLog("Android не создал локальную точку доступа, код $reason")
            }
            }, Handler(Looper.getMainLooper()))
        } catch (error: SecurityException) {
            clearPhoneBeaconHotspotIp(requestSessionId)
            if (hotspotSessionId == requestSessionId) hotspotSessionId = null
            hotspotStatusView.text = "Нет системного разрешения Wi-Fi: ${error.message}"
            hotspotOpenButton.isEnabled = true
            hotspotOpenButton.visibility = View.VISIBLE
            appendLog("Android запретил создание точки доступа: нет CHANGE_WIFI_STATE")
            setWebUiConnectionState(WebUiConnectionState.ERROR)
        }
    }

    private fun closeLocalHotspot(reason: String) {
        clearPhoneBeaconHotspotIp(hotspotSessionId)
        localHotspotReservation?.close()
        localHotspotReservation = null
        hotspotSessionId = null
        hotspotCredentialWriteInFlight = false
        pendingHotspotWifi = null
        hotspotExpectedEspIp = null
        hotspotOpenedEspIp = null
        hotspotIpCheckPending = false
        webUiRequestPending = false
        hotspotProvisioningStarted = false
        runtimeWebPending = false
        signalHandler.removeCallbacks(hotspotNoClientWatchdog)
        signalHandler.removeCallbacks(otaStatusPoll)
        signalHandler.removeCallbacks(webUiCountdown)
        finishWebUiIpRequest()
        hotspotStatusView.setText(R.string.local_hotspot_status)
        hotspotWebUrlView.text = ""
        hotspotWebUrlView.tag = null
        hotspotWebUrlView.visibility = View.GONE
        setWebUiConnectionState(WebUiConnectionState.ERROR)
        appendLog("Локальная точка доступа отключена: $reason")
    }

    private fun sendHotspotCredentials(
        reservation: WifiManager.LocalOnlyHotspotReservation,
        hotspotIp: ByteArray?,
        sessionId: String? = hotspotSessionId
    ) {
        val credentials = runCatching {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                val config = reservation.softApConfiguration
                config.ssid to config.passphrase
            } else {
                val config = reservation.wifiConfiguration
                config?.SSID?.trim('"') to config?.preSharedKey?.trim('"')
            }
        }.getOrNull()
        val ssid = credentials?.first
        val password = credentials?.second
        if (ssid.isNullOrBlank() || password.isNullOrBlank()) {
            webUiRequestPending = false
            hotspotProvisioningStarted = false
            setWebUiConnectionState(WebUiConnectionState.ERROR)
            appendLog("Android не сообщил параметры точки доступа")
            return
        }
        pendingHotspotWifi = "$ssid\n$password"
        val intent = Intent(this, BleLockService::class.java)
            .setAction(BleLockService.ACTION_WIFI_PROVISION)
            .putExtra(BleLockService.EXTRA_WIFI_SSID, ssid)
            .putExtra(BleLockService.EXTRA_WIFI_PASSWORD, password)
            .putExtra(BleLockService.EXTRA_HOTSPOT_IP, hotspotIp)
            .putExtra(BleLockService.EXTRA_HOTSPOT_SESSION, sessionId)
        runCatching { startService(intent) }
            .onSuccess {
                hotspotCredentialWriteInFlight = true
                pendingHotspotWifi = null
                setWebUiConnectionState(WebUiConnectionState.SENDING_WIFI_CREDENTIALS)
                hotspotStatusView.text = "Hotspot готов, ожидается подключение ESP32"
                appendLog("BLE-маяк переключён в режим передачи Wi-Fi credentials")
            }
            .onFailure {
                hotspotCredentialWriteInFlight = false
                setWebUiConnectionState(WebUiConnectionState.ERROR)
                appendLog("Не удалось запустить BLE provisioning: ${it.message}")
                Toast.makeText(this, "Не удалось запустить BLE provisioning", Toast.LENGTH_LONG).show()
            }
    }

    private fun clearPhoneBeaconHotspotIp(sessionId: String? = hotspotSessionId) {
        sendBroadcast(
            Intent(BleLockService.ACTION_CLEAR_HOTSPOT_IP)
                .setPackage(packageName)
                .putExtra(BleLockService.EXTRA_HOTSPOT_SESSION, sessionId)
        )
    }

    private fun resolveHotspotIpv4(): java.net.Inet4Address? {
        val connectivity = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        val stationWifiActive = runCatching {
            connectivity.allNetworks.any { network ->
                connectivity.getNetworkCapabilities(network)
                    ?.hasTransport(android.net.NetworkCapabilities.TRANSPORT_WIFI) == true
            }
        }.onFailure {
            appendLog("Не удалось определить активный Wi-Fi интерфейс: ${it.message}")
        }.getOrDefault(true)
        return HotspotIpv4Resolver.resolve(allowWlanFallback = !stationWifiActive, logResult = false)
    }

    private fun resolveHotspotIpv4ThenSend(
        reservation: WifiManager.LocalOnlyHotspotReservation,
        sessionId: String?,
        attemptsLeft: Int = HOTSPOT_INTERFACE_RESOLVE_ATTEMPTS
    ) {
        if (sessionId == null || sessionId != hotspotSessionId || localHotspotReservation !== reservation) return
        val address = resolveHotspotIpv4()
        if (address != null) {
            appendLog("IPv4 hotspot найден: ${address.hostAddress}; отправляю credentials через PhoneBeacon")
            sendHotspotCredentials(reservation, address.address, sessionId)
            return
        }
        if (attemptsLeft > 1) {
            signalHandler.postDelayed(
                { resolveHotspotIpv4ThenSend(reservation, sessionId, attemptsLeft - 1) },
                HOTSPOT_INTERFACE_RESOLVE_INTERVAL_MS
            )
            return
        }
        // Record one final resolver diagnostic, then continue the existing SSID/password fallback.
        val connectivity = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        val stationWifiActive = runCatching {
            connectivity.allNetworks.any { network ->
                connectivity.getNetworkCapabilities(network)
                    ?.hasTransport(android.net.NetworkCapabilities.TRANSPORT_WIFI) == true
            }
        }.getOrDefault(true)
        HotspotIpv4Resolver.resolve(allowWlanFallback = !stationWifiActive, logResult = true)
        appendLog("IPv4 hotspot не определён за ${HOTSPOT_INTERFACE_RESOLVE_ATTEMPTS * HOTSPOT_INTERFACE_RESOLVE_INTERVAL_MS / 1000}s; ESP получит SSID/пароль и продолжит через fallback")
        sendHotspotCredentials(reservation, null, sessionId)
    }

    private fun trySendPendingHotspotCredentials(): Boolean {
        val payload = pendingHotspotWifi ?: return false
        if (hotspotCredentialWriteInFlight || repository.currentState != BleRepository.State.READY) {
            return false
        }
        hotspotCredentialWriteInFlight = true
        appendLog("Передаю настройки точки доступа в ESP32 по BLE")
        repository.writeOtaConfig(payload)
        return true
    }

    private fun hotspotPermissionsGranted(): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return false
        val permission = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            Manifest.permission.NEARBY_WIFI_DEVICES
        } else {
            Manifest.permission.ACCESS_FINE_LOCATION
        }
        return checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED
    }

    private fun setWebUiConnectionState(state: WebUiConnectionState) {
        webUiConnectionState = state
        if (!::hotspotOpenButton.isInitialized) return
        when (state) {
            WebUiConnectionState.IDLE -> hotspotOpenButton.setText(R.string.control_settings)
            WebUiConnectionState.COMPLETE -> {
                signalHandler.removeCallbacks(webUiCountdown)
                hotspotOpenButton.setText(R.string.control_settings)
            }
            WebUiConnectionState.ERROR -> {
                signalHandler.removeCallbacks(webUiCountdown)
                hotspotOpenButton.setText(R.string.control_settings)
            }
            WebUiConnectionState.STARTING_HOTSPOT -> hotspotOpenButton.text = "Создание Wi-Fi сети…"
            WebUiConnectionState.SENDING_WIFI_CREDENTIALS,
            WebUiConnectionState.ESP_CONNECTING,
            WebUiConnectionState.WAITING_WEBUI_ADDRESS,
            WebUiConnectionState.OPENING_BROWSER -> hotspotOpenButton.text = "Ожидание ESP32…"
        }
    }


    private fun requiredPermissions(): List<String> {
        val permissions = mutableListOf<String>()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            permissions += Manifest.permission.BLUETOOTH_SCAN
            permissions += Manifest.permission.BLUETOOTH_CONNECT
            permissions += Manifest.permission.BLUETOOTH_ADVERTISE
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
        private const val KEY_BEACON_POWER_SCREEN_VISIBLE = "beacon_power_screen_visible"
        private const val SCREEN_TRANSITION_DURATION_MS = 220L
        private const val REQUEST_BLE_PERMISSIONS = 100
        private const val REQUEST_HOTSPOT_PERMISSIONS = 101
        private const val ADMIN_SIGNAL_POLL_INTERVAL_MS = 2_000L
        private const val SERVICE_RESUME_DELAY_MS = 700L
        private const val HOTSPOT_IP_CHECK_INTERVAL_MS = 10_000L
        private const val HOTSPOT_PROVISIONING_TIMEOUT_SECONDS = 60
        private const val HOTSPOT_PROVISIONING_TIMEOUT_MS = 60_000L
        private const val HOTSPOT_INTERFACE_RESOLVE_ATTEMPTS = 20
        private const val HOTSPOT_INTERFACE_RESOLVE_INTERVAL_MS = 250L
        private const val ADMIN_PROBE_RETRY_DELAY_MS = 500L
        private const val MAX_ADMIN_PROBE_RETRIES = 3
        private const val MANUAL_CONNECT_DELAY_MS = 1_500L
        private const val SWIPE_REFRESH_TIMEOUT_MS = 4_000L
        private const val MAX_BONDED_KEYS = 20
        private const val DEFAULT_ADV_TX_POWER_DBM = 0
        private val ADV_TX_POWER_VALUES = intArrayOf(-12, -9, -6, -3, 0, 3, 6, 9)
        private val ADV_TX_POWER_LABELS = arrayOf(
            "−12 dBm", "−9 dBm", "−6 dBm", "−3 dBm",
            "0 dBm", "+3 dBm", "+6 dBm", "+9 dBm"
        )
    }
}

private fun Any?.asLongOrNull(): Long? = when (this) {
    is Number -> toLong()
    else -> null
}

private fun String?.isValidIpv4(): Boolean {
    val octets = this?.split('.') ?: return false
    if (octets.size != 4 || octets.any { part ->
            part.isEmpty() || part.length > 3 || part.any { !it.isDigit() } ||
                part.toIntOrNull()?.let { it !in 0..255 } != false
        }) return false
    val first = octets[0].toInt()
    return first != 0 && first != 127 && octets.any { it.toInt() != 0 }
}
