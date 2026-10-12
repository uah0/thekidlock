package com.blelock.app

import android.app.LocaleManager
import android.content.Context
import android.content.res.Configuration
import android.os.Build
import android.os.LocaleList
import java.util.Locale

internal object AppLanguage {
    private const val PREFS = "app_language"
    private const val TAG = "language_tag"
    val supportedTags = listOf("en", "ru", "uk", "pl", "be", "lt", "lv", "de", "ja")

    fun currentTag(context: Context): String {
        if (Build.VERSION.SDK_INT >= 33) {
            val appTag = context.getSystemService(LocaleManager::class.java)
                ?.applicationLocales?.get(0)?.language
            if (appTag != null) return supportedTags.firstOrNull { it == appTag } ?: "en"
        } else {
            val saved = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .getString(TAG, null)
            if (saved != null) return supportedTags.firstOrNull { it == saved } ?: "en"
        }
        val systemTag = context.resources.configuration.locales[0]?.language ?: "en"
        return supportedTags.firstOrNull { it == systemTag } ?: "en"
    }

    fun select(context: Context, tag: String) {
        require(tag in supportedTags)
        if (Build.VERSION.SDK_INT >= 33) {
            context.getSystemService(LocaleManager::class.java)
                ?.applicationLocales = LocaleList.forLanguageTags(tag)
        } else {
            context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putString(TAG, tag).apply()
        }
    }

    fun wrap(context: Context): Context {
        if (Build.VERSION.SDK_INT >= 33) return context
        val tag = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(TAG, null)
            ?: return context
        if (tag !in supportedTags) return context
        val configuration = Configuration(context.resources.configuration)
        configuration.setLocale(Locale.forLanguageTag(tag))
        return context.createConfigurationContext(configuration)
    }
}
