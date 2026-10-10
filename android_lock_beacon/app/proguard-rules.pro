# Release hardening rules.
# Paho creates this logger through Class.forName(...).newInstance().
-keep class org.eclipse.paho.client.mqttv3.logging.JSR47Logger { *; }
-keep class com.blelock.app.AppUpdateWorker {
    public <init>(android.content.Context, androidx.work.WorkerParameters);
}
# Remove application Logcat diagnostics from optimized release builds while
# retaining them in debug builds for hardware/BLE troubleshooting.
-assumenosideeffects class android.util.Log {
    public static *** v(...);
    public static *** d(...);
    public static *** i(...);
    public static *** w(...);
    public static *** e(...);
    public static *** wtf(...);
}
