package com.bleforeva.diagnostic

import android.Manifest
import android.app.Activity
import android.bluetooth.BluetoothManager
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import java.time.Duration
import java.time.Instant

class MainActivity : Activity() {
    private lateinit var info: TextView
    private val handler = Handler(Looper.getMainLooper())
    private val refresh = object : Runnable {
        override fun run() { render(); handler.postDelayed(this, 1000) }
    }
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        BeaconState.log(this, "APP_START", BeaconState.deviceInfo())
        val column = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(24, 24, 24, 24) }
        val title = TextView(this).apply { text = "BLE Foreva"; textSize = 26f }
        column.addView(title)
        info = TextView(this).apply { textSize = 16f }
        column.addView(info)
        column.addView(TextView(this).apply { text = "Advertising mode for next START:"; textSize = 16f })
        val modeGroup = RadioGroup(this).apply { orientation = RadioGroup.VERTICAL }
        val selectedMode = BeaconMode.from(BeaconState.prefs(this).getString("selected_mode", null))
        val modeButtons = BeaconMode.entries.associateWith { mode ->
            RadioButton(this).apply { id = android.view.View.generateViewId(); text = mode.label }
        }
        modeButtons.forEach { (mode, button) ->
            modeGroup.addView(button)
            if (mode == selectedMode) modeGroup.check(button.id)
        }
        modeGroup.setOnCheckedChangeListener { _, checkedId ->
            val mode = modeButtons.entries.firstOrNull { it.value.id == checkedId }?.key ?: return@setOnCheckedChangeListener
            BeaconState.prefs(this).edit().putString("selected_mode", mode.name).apply()
            BeaconState.log(this, "MODE_SELECTED", mode.name)
            render()
        }
        column.addView(modeGroup)
        fun button(label: String, action: () -> Unit) {
            column.addView(Button(this).apply { text = label; setOnClickListener { action() } },
                LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        }
        button("START BEACON") { startWithPermissions() }
        button("STOP BEACON") {
            startService(Intent(this, BeaconForegroundService::class.java).setAction(BeaconForegroundService.STOP))
        }
        button("COPY LOG") {
            val log = BeaconState.logFile(this).takeIf { it.exists() }?.readText().orEmpty()
            getSystemService(ClipboardManager::class.java).setPrimaryClip(ClipData.newPlainText("BLE Foreva log", log))
            Toast.makeText(this, "Log copied", Toast.LENGTH_SHORT).show()
        }
        button("EXPORT LOG") {
            startActivityForResult(Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
                addCategory(Intent.CATEGORY_OPENABLE); type = "text/plain"
                putExtra(Intent.EXTRA_TITLE, "ble-foreva.log")
            }, 2)
        }
        button("CLEAR LOG") {
            BeaconState.logFile(this).writeText("")
            BeaconState.log(this, "LOG_CLEARED")
            render()
        }
        setContentView(ScrollView(this).apply { addView(column) })
    }
    override fun onResume() { super.onResume(); handler.post(refresh) }
    override fun onPause() { handler.removeCallbacks(refresh); super.onPause() }

    private fun startWithPermissions() {
        val needed = mutableListOf<String>()
        if (Build.VERSION.SDK_INT >= 31) {
            needed += Manifest.permission.BLUETOOTH_ADVERTISE
            needed += Manifest.permission.BLUETOOTH_CONNECT
        }
        if (Build.VERSION.SDK_INT >= 33) needed += Manifest.permission.POST_NOTIFICATIONS
        val missing = needed.filter { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }
        if (missing.isNotEmpty()) requestPermissions(missing.toTypedArray(), 1) else startBeacon()
    }
    private fun startBeacon() {
        try { startForegroundService(Intent(this, BeaconForegroundService::class.java).setAction(BeaconForegroundService.START)) }
        catch (e: Exception) {
            BeaconState.log(this, "SERVICE_START_FAILURE", e.toString())
            Toast.makeText(this, e.message ?: "Service start failed", Toast.LENGTH_LONG).show()
        }
    }
    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, results: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, results)
        if (requestCode == 1) {
            if (results.isNotEmpty() && results.all { it == PackageManager.PERMISSION_GRANTED }) startBeacon()
            else { BeaconState.log(this, "PERMISSION_DENIED", permissions.joinToString());
                Toast.makeText(this, "Bluetooth and notification permissions are required", Toast.LENGTH_LONG).show() }
        }
    }
    @Deprecated("Android activity result API")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == 2 && resultCode == RESULT_OK && data?.data != null) {
            try {
                contentResolver.openOutputStream(data.data!!)?.use { out ->
                    val log = BeaconState.logFile(this)
                    if (log.exists()) log.inputStream().use { it.copyTo(out) }
                }
                Toast.makeText(this, "Log exported", Toast.LENGTH_SHORT).show()
            } catch (e: Exception) {
                Toast.makeText(this, "Export failed: ${e.message}", Toast.LENGTH_LONG).show()
            }
        }
    }
    private fun render() {
        val p = BeaconState.prefs(this)
        val adapter = getSystemService(BluetoothManager::class.java).adapter
        val bt = try { if (adapter?.isEnabled == true) "ON" else "OFF" } catch (_: SecurityException) { "permission required" }
        val adv = try { if (adapter?.bluetoothLeAdvertiser != null) "YES" else "NO" }
            catch (_: SecurityException) { "permission required" }
        val multiple = try { if (adapter?.isMultipleAdvertisementSupported == true) "YES" else "NO" }
            catch (_: SecurityException) { "permission required" }
        val started = p.getString("started", "") ?: ""
        val running = try {
            val s = Duration.between(Instant.parse(started), Instant.now()).seconds.coerceAtLeast(0)
            "%02d:%02d:%02d".format(s / 3600, s / 60 % 60, s % 60)
        } catch (_: Exception) { "--:--:--" }
        val screen = if (getSystemService(PowerManager::class.java).isInteractive) "ON" else "OFF"
        val lastLog = BeaconState.logFile(this).takeIf { it.exists() }?.readLines()?.takeLast(8)?.joinToString("\n").orEmpty()
        info.text = """
            Bluetooth: $bt
            Advertiser supported: $adv
            Multiple advertisement supported: $multiple

            Beacon status: ${p.getString("status", "STOPPED")}
            Active advertising mode: ${p.getString("active_mode", "LOW_POWER")}
            Device ID: ${BeaconState.hex(BeaconState.deviceId(this))}
            Session ID: ${if (started.isEmpty()) "--------" else BeaconState.hex(p.getInt("session", 0))}
            Started: $started
            Running: $running
            Screen: $screen
            Last advertiser callback: ${p.getString("callback", "--")}
            Last error: ${p.getString("error", "--")}

            Recent log:
            $lastLog
        """.trimIndent()
    }
}
