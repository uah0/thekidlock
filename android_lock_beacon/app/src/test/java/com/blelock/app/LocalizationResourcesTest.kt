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
        for (language in listOf("values-en", "values-ru", "values-uk")) {
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

    @Test fun defaultLanguageIsEnglishAndRussianAndUkrainianAreDistinct() {
        val english = strings("values")
        assertFalse(english.values.flatten().any { Regex("[А-Яа-яЁёІіЇїЄє]").containsMatchIn(it) })
        assertEquals(listOf("Lock settings"), english.getValue("control_settings"))
        assertEquals(listOf("Настройки замка"), strings("values-ru").getValue("control_settings"))
        assertEquals(listOf("Налаштування замка"), strings("values-uk").getValue("control_settings"))
        assertEquals(listOf("(доступне оновлення)"), strings("values-uk").getValue("firmware_update_available"))
    }

    @Test fun systemLanguagePickerDeclaresExactlySupportedLanguages() {
        val entries = document("xml/locales_config.xml").getElementsByTagName("locale")
        val languages = List(entries.length) {
            (entries.item(it) as Element).getAttribute("android:name")
        }
        assertEquals(listOf("en", "ru", "uk"), languages)
    }
}
