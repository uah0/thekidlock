"""Проверка отражённой MQTT-фабрики из release APK на телефоне, без сети/открытия."""
import argparse
from pathlib import Path
import os
import re
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--serial", required=True)
    parser.add_argument("--factory", help="Mapped logger factory for the installed APK")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    sdk = Path(r"C:\Dev\AndroidSdk")
    java = Path(r"C:\Program Files\Android\Android Studio\jbr\bin")
    if args.factory:
        factory = args.factory
    else:
        mapping = (root / "android_lock_beacon/app/build/outputs/mapping/release/mapping.txt").read_text()
        factory = re.search(r"^org\.eclipse\.paho\.client\.mqttv3\.logging\.LoggerFactory -> (.*):$", mapping, re.M)[1]
    work = root / ".device-agent/mqtt-release-smoke"
    work.mkdir(parents=True, exist_ok=True)
    source = work / "ReleaseMqttSmoke.java"
    source.write_text('''
package com.blelock.mqttsmoke;
import android.app.Instrumentation;
import android.os.Bundle;
import java.lang.reflect.*;
public class ReleaseMqttSmoke extends Instrumentation {
    private String factoryName;
    public void onCreate(Bundle arguments) {
        super.onCreate(arguments);
        factoryName = arguments.getString("factory");
        start();
    }
    public void onStart() {
        Bundle result = new Bundle();
        try {
            checkFactory();
            result.putString("status", "PASS: release APK MQTT logger instantiated");
            finish(-1, result);
        } catch (Throwable error) {
            Throwable cause = error;
            while (cause.getCause() != null) cause = cause.getCause();
            result.putString("error", cause.getClass().getName() + ": " + cause.getMessage());
            finish(0, result);
        }
    }
    private void checkFactory() throws Exception {
        Class<?> factory = Class.forName(factoryName, true, getTargetContext().getClassLoader());
        for (Method method : factory.getDeclaredMethods()) {
            Class<?>[] types = method.getParameterTypes();
            if (!Modifier.isStatic(method.getModifiers()) || types.length < 1 || types.length > 2) continue;
            boolean strings = true;
            for (Class<?> type : types) strings &= type == String.class;
            if (!strings) continue;
            method.setAccessible(true);
            try {
                Object logger = types.length == 1 ? method.invoke(null, "release-smoke") :
                    method.invoke(null, "org.eclipse.paho.client.mqttv3.internal.nls.logcat", "release-smoke");
                if (logger == null) throw new IllegalStateException("MQTT logger factory returned null");
                return;
            } catch (InvocationTargetException error) {
                throw new IllegalStateException("MQTT initialization failed", error.getCause());
            }
        }
        throw new IllegalStateException("MQTT logger factory entry point missing");
    }
}
''', encoding="utf-8")
    android = sdk / "platforms/android-36/android.jar"
    subprocess.run([str(java / "javac.exe"), "-source", "8", "-target", "8", "-classpath", str(android), str(source)], check=True, capture_output=True)
    subprocess.run([str(java / "java.exe"), "-cp", str(sdk / "build-tools/36.0.0/lib/d8.jar"),
                    "com.android.tools.r8.D8", "--min-api", "26", "--lib", str(android),
                    "--output", str(work), str(work / "ReleaseMqttSmoke.class")], check=True, capture_output=True)
    manifest = work / "AndroidManifest.xml"
    manifest.write_text('''<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="com.blelock.mqttsmoke">
      <uses-sdk android:minSdkVersion="26" android:targetSdkVersion="36"/>
      <application android:label="TheKidLock MQTT smoke"/>
      <instrumentation android:name="com.blelock.mqttsmoke.ReleaseMqttSmoke" android:targetPackage="com.blelock.app"/>
    </manifest>''', encoding="utf-8")
    unsigned = work / "unsigned.apk"
    subprocess.run([str(sdk / "build-tools/36.0.0/aapt.exe"), "package", "-f", "-M", str(manifest),
                    "-I", str(android), "-F", str(unsigned)], check=True, capture_output=True)
    import zipfile
    with zipfile.ZipFile(unsigned, "a") as archive:
        archive.write(work / "classes.dex", "classes.dex")
    properties = dict(line.split("=", 1) for line in
                      (root / "android_lock_beacon/keystore.properties").read_text().splitlines() if "=" in line)
    env = os.environ.copy()
    env["THEKIDLOCK_KEYSTORE_PASSWORD"] = properties["storePassword"]
    env["THEKIDLOCK_KEY_PASSWORD"] = properties["keyPassword"]
    key = (root / "android_lock_beacon/app" / properties["storeFile"]).resolve()
    signed = work / "mqtt-smoke.apk"
    subprocess.run([str(java / "java.exe"), "-jar", str(sdk / "build-tools/36.0.0/lib/apksigner.jar"),
                    "sign", "--ks", str(key), "--ks-key-alias", properties["keyAlias"],
                    "--ks-pass", "env:THEKIDLOCK_KEYSTORE_PASSWORD", "--key-pass", "env:THEKIDLOCK_KEY_PASSWORD",
                    "--out", str(signed), str(unsigned)], env=env, check=True, capture_output=True)
    adb = [str(sdk / "platform-tools/adb.exe"), "-s", args.serial]
    def run(*command):
        result = subprocess.run(adb + list(command), capture_output=True, text=True, timeout=90)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        return result.stdout.strip()
    rotation = {key: run("shell", "settings", "get", "system", key)
                for key in ["accelerometer_rotation", "user_rotation"]}
    import json
    (work / "orientation.json").write_text(json.dumps(rotation), encoding="utf-8")
    try:
        installed = run("install", "-r", "-t", str(signed))
        if "Success" not in installed: raise RuntimeError(installed)
        arguments = ["shell", "am", "instrument", "-w", "-e", "factory", factory]
        result = subprocess.run(adb + arguments + ["com.blelock.mqttsmoke/.ReleaseMqttSmoke"],
                                capture_output=True, text=True, timeout=90)
        report = result.stdout + result.stderr
        (work / "result.txt").write_text(report, encoding="utf-8")
        print(report.strip())
        if "PASS: release APK MQTT logger instantiated" not in report:
            raise SystemExit(1)
    finally:
        subprocess.run(adb + ["uninstall", "com.blelock.mqttsmoke"], capture_output=True)
        for key, value in rotation.items(): run("shell", "settings", "put", "system", key, value)
        if any(run("shell", "settings", "get", "system", key) != value for key, value in rotation.items()):
            raise RuntimeError("Orientation restoration failed")


if __name__ == "__main__":
    main()
