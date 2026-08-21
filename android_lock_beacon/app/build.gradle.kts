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
            .format(DateTimeFormatter.ofPattern("yy-MM-dd HH:mm"))
        buildConfigField("String", "BUILD_TIMESTAMP", "\"$buildTimestamp\"")
    }

    buildTypes {
        release {
            isMinifyEnabled = false
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
    implementation("androidx.swiperefreshlayout:swiperefreshlayout:1.1.0")
}
