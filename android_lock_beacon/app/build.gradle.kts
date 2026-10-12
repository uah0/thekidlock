import java.time.LocalDateTime
import java.time.format.DateTimeFormatter
import java.util.Properties
import java.io.FileInputStream
import java.net.URI

val releaseKeystoreFile = rootProject.file("keystore.properties")
val releaseKeystore = Properties()
if (releaseKeystoreFile.exists()) {
    FileInputStream(releaseKeystoreFile).use { releaseKeystore.load(it) }
}
val releaseFirmwareUrl =
    "https://github.com/uah0/thekidlock/releases/latest/download/ble_lock_stage1_s3.bin"
val debugFirmwareUrl = providers.gradleProperty("otaFirmwareUrl").orNull ?: releaseFirmwareUrl
val debugFirmwareUri = URI(debugFirmwareUrl)
val localDebugFirmware = debugFirmwareUri.scheme == "http" &&
    debugFirmwareUri.host in setOf("127.0.0.1", "localhost")
if (debugFirmwareUrl != releaseFirmwareUrl && !localDebugFirmware) {
    throw GradleException("otaFirmwareUrl must be the pinned GitHub asset or localhost HTTP for a debug test")
}
fun buildConfigString(value: String) = "\"${value.replace("\\", "\\\\").replace("\"", "\\\"")}\""

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.blelock.app"
    compileSdk = 36

    buildFeatures {
        buildConfig = true
    }

    defaultConfig {
        applicationId = "com.blelock.app"
        minSdk = 26
        targetSdk = 36
        testInstrumentationRunner = "com.blelock.app.WebUiOtaInstrumentation"
        val buildNumber = rootProject.file("BUILD_NUMBER").readText().trim().toInt()
        versionCode = buildNumber
        versionName = "1.0.$buildNumber"
        buildConfigField("int", "BUILD_NUMBER", buildNumber.toString())
        val buildTimestamp = LocalDateTime.now()
            .format(DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss"))
        buildConfigField("String", "BUILD_TIMESTAMP", "\"$buildTimestamp\"")
        buildConfigField("String", "OTA_FIRMWARE_URL", buildConfigString(releaseFirmwareUrl))
    }

    buildTypes {
        debug {
            buildConfigField("boolean", "ENABLE_DIAGNOSTIC_LOGS", "true")
            buildConfigField("String", "OTA_FIRMWARE_URL", buildConfigString(debugFirmwareUrl))
        }
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            buildConfigField("boolean", "ENABLE_DIAGNOSTIC_LOGS", "false")
            buildConfigField("String", "OTA_FIRMWARE_URL", buildConfigString(releaseFirmwareUrl))
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            if (releaseKeystoreFile.exists()) {
                signingConfig = signingConfigs.create("playRelease")
                signingConfig?.storeFile = file(releaseKeystore.getProperty("storeFile"))
                signingConfig?.storePassword = releaseKeystore.getProperty("storePassword")
                signingConfig?.keyAlias = releaseKeystore.getProperty("keyAlias")
                signingConfig?.keyPassword = releaseKeystore.getProperty("keyPassword")
            }
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
}

dependencies {
    implementation("org.bouncycastle:bcprov-jdk18on:1.79")
    implementation("org.eclipse.paho:org.eclipse.paho.client.mqttv3:1.2.5")
    implementation("androidx.core:core:1.13.1")
    implementation("androidx.swiperefreshlayout:swiperefreshlayout:1.1.0")
    implementation("androidx.work:work-runtime:2.10.5")
    testImplementation("junit:junit:4.13.2")
}
