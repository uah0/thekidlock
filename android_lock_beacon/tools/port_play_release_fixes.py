from pathlib import Path

ROOT = Path("android_lock_beacon")


def replace(path: Path, old: str, new: str, count: int = 1):
    text = path.read_text(encoding="utf-8")
    if old not in text:
        if new in text:
            return
        raise SystemExit(f"Expected block not found in {path}: {old[:120]!r}")
    path.write_text(text.replace(old, new, count), encoding="utf-8")

# Gradle release hardening + ContextCompat dependency.
p = ROOT / "app/build.gradle.kts"
replace(p,
'''    buildTypes {
        release {
            isMinifyEnabled = false
''',
'''    buildTypes {
        debug {
            buildConfigField("boolean", "ENABLE_DIAGNOSTIC_LOGS", "true")
        }
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            buildConfigField("boolean", "ENABLE_DIAGNOSTIC_LOGS", "false")
''')
replace(p,
'''dependencies {
    implementation("androidx.swiperefreshlayout:swiperefreshlayout:1.1.0")
}''',
'''dependencies {
    implementation("androidx.core:core:1.13.1")
    implementation("androidx.swiperefreshlayout:swiperefreshlayout:1.1.0")
}''')

# Strip Logcat calls from optimized release builds.
(ROOT / "app/proguard-rules.pro").write_text('''# Release hardening rules.\n# Remove application Logcat diagnostics from optimized release builds while\n# retaining them in debug builds for hardware/BLE troubleshooting.\n-assumenosideeffects class android.util.Log {\n    public static *** v(...);\n    public static *** d(...);\n    public static *** i(...);\n    public static *** w(...);\n    public static *** e(...);\n    public static *** wtf(...);\n}\n''', encoding="utf-8")

# Manifest: legacy coarse location pairing; remove direct battery exemption permission.
p = ROOT / "app/src/main/AndroidManifest.xml"
replace(p,
'''    <uses-permission android:name="android.permission.BLUETOOTH_ADMIN" android:maxSdkVersion="30" />\n    <uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" android:maxSdkVersion="32" />''',
'''    <uses-permission android:name="android.permission.BLUETOOTH_ADMIN" android:maxSdkVersion="30" />\n    <uses-permission android:name="android.permission.ACCESS_COARSE_LOCATION" android:maxSdkVersion="32" />\n    <uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" android:maxSdkVersion="32" />''')
text = p.read_text(encoding="utf-8")
text = text.replace('    <uses-permission android:name="android.permission.REQUEST_IGNORE_BATTERY_OPTIMIZATIONS" />\n', '')
p.write_text(text, encoding="utf-8")

# Recover automatic mode after application update.
p = ROOT / "app/src/main/java/com/blelock/app/BootReceiver.kt"
replace(p,
'''        if (!regularBoot && !lockedBoot) return''',
'''        val packageReplaced = intent.action == Intent.ACTION_MY_PACKAGE_REPLACED\n        if (!regularBoot && !lockedBoot && !packageReplaced) return''')

# Android 13+ GATT write status constant.
p = ROOT / "app/src/main/java/com/blelock/app/BleRepository.kt"
replace(p,
'''import android.bluetooth.BluetoothProfile\nimport android.bluetooth.le.ScanCallback''',
'''import android.bluetooth.BluetoothProfile\nimport android.bluetooth.BluetoothStatusCodes\nimport android.bluetooth.le.ScanCallback''')
replace(p,
'''                ) == BluetoothGatt.GATT_SUCCESS''',
'''                ) == BluetoothStatusCodes.SUCCESS''')

# MainActivity: compatibility, safe receiver, battery settings, clean footer.
p = ROOT / "app/src/main/java/com/blelock/app/MainActivity.kt"
text = p.read_text(encoding="utf-8")
text = text.replace('import android.os.PowerManager\n', '')
if 'import androidx.core.content.ContextCompat\n' not in text:
    text = text.replace('import android.widget.TextView\n', 'import android.widget.TextView\nimport androidx.core.content.ContextCompat\n')
text = text.replace('    private lateinit var firmwareVersionView: TextView\n', '')
text = text.replace('    private lateinit var appVersionView: TextView\n', '')
text = text.replace('''                if (!info.isNullOrBlank()) {\n                    firmwareVersionView.text = getString(R.string.firmware_version, info)\n                }\n''', '')
text = text.replace('''        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {\n            registerReceiver(backgroundConnectionReceiver, filter, RECEIVER_NOT_EXPORTED)\n        } else {\n            registerReceiver(backgroundConnectionReceiver, filter)\n        }''', '''        ContextCompat.registerReceiver(\n            this,\n            backgroundConnectionReceiver,\n            filter,\n            ContextCompat.RECEIVER_NOT_EXPORTED\n        )''')
text = text.replace('''        firmwareVersionView = TextView(this).apply {\n            text = getString(R.string.firmware_version_unknown)\n            textSize = 14f\n            setTextColor(getColor(R.color.secondary_text))\n        }\n        appVersionView = TextView(this).apply {\n            text = getString(\n                R.string.app_version_release,\n                BuildConfig.VERSION_NAME,\n                BuildConfig.VERSION_CODE,\n                BuildConfig.BUILD_TIMESTAMP\n            )\n            textSize = 14f\n            setTextColor(getColor(R.color.secondary_text))\n        }\n''', '')
text = text.replace('''        root.addView(firmwareVersionView, topSpacedFullWidth(12).apply { leftMargin = dp(4) })\n        root.addView(appVersionView, topSpacedFullWidth(12).apply { leftMargin = dp(4) })\n''', '')
text = text.replace('''        root.addView(privacyPolicyButton, topSpacedFullWidth(8))\n\n        adminContainer.addView(Space(this), fullWidth().apply { height = dp(32) })''', '''        adminContainer.addView(Space(this), fullWidth().apply { height = dp(32) })''')
text = text.replace('''                val systemBars = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {\n                    insets.getInsets(WindowInsets.Type.systemBars())\n                } else {\n                    null\n                }\n                val topInset = systemBars?.top ?: insets.systemWindowInsetTop\n                val bottomInset = systemBars?.bottom ?: insets.systemWindowInsetBottom\n                view.setPadding(0, topInset, 0, bottomInset)''', '''                val topInset: Int\n                val bottomInset: Int\n                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {\n                    val systemBars = insets.getInsets(WindowInsets.Type.systemBars())\n                    topInset = systemBars.top\n                    bottomInset = systemBars.bottom\n                } else {\n                    @Suppress("DEPRECATION")\n                    topInset = insets.systemWindowInsetTop\n                    @Suppress("DEPRECATION")\n                    bottomInset = insets.systemWindowInsetBottom\n                }\n                view.setPadding(0, topInset, 0, bottomInset)''')
text = text.replace('''            addView(swipeRefresh, FrameLayout.LayoutParams(\n                ViewGroup.LayoutParams.MATCH_PARENT,\n                ViewGroup.LayoutParams.MATCH_PARENT\n            ))\n            post { requestApplyInsets() }''', '''            addView(swipeRefresh, FrameLayout.LayoutParams(\n                ViewGroup.LayoutParams.MATCH_PARENT,\n                ViewGroup.LayoutParams.MATCH_PARENT\n            ).apply { bottomMargin = dp(44) })\n            addView(privacyPolicyButton, FrameLayout.LayoutParams(\n                ViewGroup.LayoutParams.MATCH_PARENT,\n                ViewGroup.LayoutParams.WRAP_CONTENT,\n                Gravity.BOTTOM\n            ))\n            post { requestApplyInsets() }''')
text = text.replace('''        if (locationManager.isLocationEnabled || legacyLocationDialog?.isShowing == true) return''', '''        val locationEnabled = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {\n            locationManager.isLocationEnabled\n        } else {\n            @Suppress("DEPRECATION")\n            Settings.Secure.getInt(\n                contentResolver,\n                Settings.Secure.LOCATION_MODE,\n                Settings.Secure.LOCATION_MODE_OFF\n            ) != Settings.Secure.LOCATION_MODE_OFF\n        }\n        if (locationEnabled || legacyLocationDialog?.isShowing == true) return''')
text = text.replace('requestIgnoreBatteryOptimizations()', 'openBatteryOptimizationSettings()')
text = text.replace('''    private fun requestIgnoreBatteryOptimizations() {\n        val powerManager = getSystemService(PowerManager::class.java)\n        if (powerManager?.isIgnoringBatteryOptimizations(packageName) == true) return\n        startActivity(Intent(\n            Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS,\n            Uri.parse("package:$packageName")\n        ))\n    }''', '''    private fun openBatteryOptimizationSettings() {\n        val intent = Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS)\n        runCatching { startActivity(intent) }\n            .onFailure { startActivity(Intent(Settings.ACTION_SETTINGS)) }\n    }''')
p.write_text(text, encoding="utf-8")

# Move API-27-only navigation-bar light flag out of the base resource set.
p = ROOT / "app/src/main/res/values/styles.xml"
base = p.read_text(encoding="utf-8")
base = base.replace('        <item name="android:windowLightNavigationBar">true</item>\n', '')
p.write_text(base, encoding="utf-8")
v27 = ROOT / "app/src/main/res/values-v27/styles.xml"
v27.parent.mkdir(parents=True, exist_ok=True)
v27.write_text(base.replace('        <item name="android:windowLightStatusBar">true</item>\n', '        <item name="android:windowLightNavigationBar">true</item>\n        <item name="android:windowLightStatusBar">true</item>\n'), encoding="utf-8")

print("Ported production-safe release fixes without Base64 background or BleLockApplication")
