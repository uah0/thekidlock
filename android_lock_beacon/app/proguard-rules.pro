# Release hardening rules.
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
