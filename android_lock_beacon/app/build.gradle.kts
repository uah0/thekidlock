import java.time.LocalDateTime
import java.time.format.DateTimeFormatter
import java.util.Properties
import java.io.FileInputStream

val releaseKeystoreFile = rootProject.file("keystore.properties")
val releaseKeystore = Properties()
if (releaseKeystoreFile.exists()) {
    FileInputStream(releaseKeystoreFile).use { releaseKeystore.load(it) }
}

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
        versionCode = 1
        versionName = "1.0.0"
        val buildTimestamp = LocalDateTime.now()
            .format(DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss"))
        buildConfigField("String", "BUILD_TIMESTAMP", "\"$buildTimestamp\"")
    }

    buildTypes {
        debug {
            buildConfigField("boolean", "ENABLE_DIAGNOSTIC_LOGS", "true")
        }
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            buildConfigField("boolean", "ENABLE_DIAGNOSTIC_LOGS", "false")
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
    implementation("androidx.core:core:1.13.1")
    implementation("androidx.swiperefreshlayout:swiperefreshlayout:1.1.0")
}
