package com.blelock.app

import java.io.File
import javax.xml.parsers.DocumentBuilderFactory
import org.junit.Assert.*
import org.junit.Test
import org.w3c.dom.Element

/** Catch missing translations and format crashes before shipping an APK. */
class LocalizationResourcesTest {
    private val resourceRoot = listOf(File("src/main/res"), File("app/src/main/res"))
        .first { it.isDirectory }
    private val placeholders = Regex("%[0-9]+\\$[ds]")
    private val jsonString = Regex("\"((?:\\\\.|[^\"\\\\])*)\"\\s*:\\s*\"((?:\\\\.|[^\"\\\\])*)\"")
    private fun jsonStrings(file: File) = jsonString.findAll(file.readText()).associate {
        it.groupValues[1] to it.groupValues[2]
    }
    private fun document(path: String) = DocumentBuilderFactory.newInstance().newDocumentBuilder()
        .parse(File(resourceRoot, path)).documentElement
    private fun strings(folder: String): Map<String, List<String>> {
        val result = linkedMapOf<String, List<String>>()
        val children = document("$folder/strings.xml").childNodes
        for (index in 0 until children.length) {
            val node = children.item(index) as? Element ?: continue
            val values = if (node.tagName == "string-array") {
                val items = node.getElementsByTagName("item")
                List(items.length) { items.item(it).textContent }
            } else listOf(node.textContent)
            assertNull("Duplicate resource ${node.getAttribute("name")}", result.put(node.getAttribute("name"), values))
        }
        return result
    }

    @Test fun allLanguagesHaveCompleteResourcesAndMatchingFormatArguments() {
        val english = strings("values")
        val languages = listOf("values-en", "values-ru", "values-uk", "values-pl", "values-be", "values-lt", "values-lv", "values-de", "values-ja")
        for (language in languages) {
            val translated = strings(language)
            assertEquals(language, english.keys, translated.keys)
            for ((name, values) in english) {
                val other = translated.getValue(name)
                assertEquals("$language/$name array length", values.size, other.size)
                for (index in values.indices) {
                    assertTrue("$language/$name is empty", other[index].isNotBlank())
                    assertEquals("$language/$name format arguments",
                        placeholders.findAll(values[index]).map { it.value }.sorted().toList(),
                        placeholders.findAll(other[index]).map { it.value }.sorted().toList())
                }
            }
        }
    }

    @Test fun defaultLanguageIsEnglishAndLocalizedLanguagesAreDistinct() {
        val english = strings("values")
        assertFalse(english.values.flatten().any { Regex("[А-Яа-яЁёІіЇїЄє]").containsMatchIn(it) })
        assertEquals(listOf("Go to lock settings"), english.getValue("control_settings"))
        assertEquals(listOf("К настройкам замка"), strings("values-ru").getValue("control_settings"))
        assertEquals(listOf("До налаштувань замка"), strings("values-uk").getValue("control_settings"))
        assertEquals(listOf("(доступне оновлення)"), strings("values-uk").getValue("firmware_update_available"))
        assertEquals(listOf("Zarejestrowane telefony"), strings("values-pl").getValue("admin_bonds_title"))
        assertEquals(listOf("Sprache"), strings("values-de").getValue("app_language_title"))
        assertEquals(listOf("言語"), strings("values-ja").getValue("app_language_title"))
    }

    @Test fun systemLanguagePickerDeclaresExactlySupportedLanguages() {
        val entries = document("xml/locales_config.xml").getElementsByTagName("locale")
        val languages = List(entries.length) {
            (entries.item(it) as Element).getAttribute("android:name")
        }
        assertEquals(listOf("en", "ru", "uk", "pl", "be", "lt", "lv", "de", "ja"), languages)
    }

    @Test fun embeddedWebUiTranslationsKeepTheEnglishSchemaAndPlaceholders() {
        val assets = resourceRoot.parentFile.resolve("assets/webui/i18n")
        val english = jsonStrings(File(assets, "en.json"))
        val calibrationKeys = listOf(
            "calibration_wizard", "calibration_closed", "calibration_open", "door_not_closed_boundary",
            "starting_calibration", "calibration_title", "calibration_intro", "step_closed", "step_open",
            "repeat_instruction", "cancel", "close", "instruction_close", "instruction_open", "instruction_done",
            "speech_close", "speech_open", "speech_done", "hold_position", "listen_instruction",
            "calibration_expired", "calibration_samples", "yes", "no", "connection_error", "calibration_saved",
            "checking_closed_position", "closed_check_error", "open_value_too_low", "speech_open_value_too_low",
            "value_gap_too_small", "speech_value_gap_too_small", "calibration_save_error", "speech_unavailable",
            "api.WUI_E_CAL_SESSION_ENDED", "api.WUI_E_CAL_VERIFY_TIMEOUT", "api.WUI_E_SPEECH_FAILED",
            "api.WUI_E_SPEECH_UNAVAILABLE", "api.WUI_E_SPEECH_TEXT_INVALID"
        )
        val speechLanguages = mapOf(
            "pl" to "pl-PL", "be" to "be-BY", "lt" to "lt-LT", "lv" to "lv-LV",
            "de" to "de-DE", "ja" to "ja-JP"
        )
        for (tag in listOf("pl", "be", "lt", "lv", "de", "ja")) {
            val translated = jsonStrings(File(assets, "$tag.json"))
            assertEquals(tag, translated["locale"])
            assertEquals("$tag TTS locale", speechLanguages[tag], translated["speech_lang"])
            assertEquals(english.keys, translated.keys)
            for (key in english.keys) {
                val source = english.getValue(key)
                val value = translated.getValue(key)
                assertTrue("$tag/$key is empty", value.isNotBlank())
                val args = Regex("\\{[a-zA-Z][a-zA-Z0-9_]*}")
                assertEquals("$tag/$key placeholders", args.findAll(source).map { it.value }.sorted().toList(),
                    args.findAll(value).map { it.value }.sorted().toList())
            }
            for (key in calibrationKeys) {
                assertNotEquals("$tag/$key is still English", english.getValue(key), translated.getValue(key))
            }
        }
    }
}
