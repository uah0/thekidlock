package com.blelock.app

import org.json.JSONArray
import org.json.JSONObject

/** Validated, non-executable description of the settings screen. */
data class DeclarativeUiSchema(
    val schema: Int,
    val revision: Int,
    val title: String,
    val sections: List<DeclarativeUiSection>
) {
    companion object {
        const val SUPPORTED_SCHEMA = 1
        const val MAX_BYTES = 2048

        fun parse(raw: String): DeclarativeUiSchema {
            require(raw.toByteArray(Charsets.UTF_8).size <= MAX_BYTES) {
                "Схема превышает лимит $MAX_BYTES байт"
            }
            val root = JSONObject(raw)
            val schema = root.getInt("schema")
            require(schema == SUPPORTED_SCHEMA) { "Неподдерживаемая версия схемы: $schema" }
            val revision = root.getInt("revision")
            require(revision >= 0) { "Некорректная revision: $revision" }
            val title = root.getString("title").requireText("title")
            val sectionsJson = root.getJSONArray("sections")
            val sections = buildList {
                for (index in 0 until sectionsJson.length()) {
                    add(parseSection(sectionsJson.getJSONObject(index)))
                }
            }
            return DeclarativeUiSchema(schema, revision, title, sections)
        }

        private fun parseSection(json: JSONObject): DeclarativeUiSection {
            val id = json.getString("id").requireText("section.id")
            val title = json.getString("title").requireText("section.title")
            val itemsJson = json.getJSONArray("items")
            val items = buildList {
                for (index in 0 until itemsJson.length()) {
                    val item = parseItem(itemsJson.getJSONObject(index))
                    if (item != null) add(item)
                }
            }
            return DeclarativeUiSection(id, title, items)
        }

        private fun parseItem(json: JSONObject): DeclarativeUiItem? {
            val uiType = json.getString("ui_type")
            val type = json.getString("type")
            if (!isSupported(uiType, type)) return null

            val id = json.getString("id").requireText("item.id")
            val title = json.getString("title").requireText("item.title")
            val readOnly = json.getBoolean("read_only")
            val value = json.opt("value").takeUnless { it === JSONObject.NULL }
            val min = json.optionalLong("min")
            val max = json.optionalLong("max")
            val step = json.optionalLong("step")
            if (uiType == "slider") {
                require(min != null && max != null && step != null) { "У slider нет min/max/step: $id" }
                require(min <= max && step > 0) { "Некорректный диапазон slider: $id" }
                val numericValue = value.asLong("item.value: $id")
                require(numericValue in min..max && (numericValue - min) % step == 0L) {
                    "Значение slider вне диапазона: $id"
                }
            }
            if (uiType == "switch") require(value is Boolean) { "switch требует boolean: $id" }
            if (uiType == "text") require(value is String) { "text требует string: $id" }
            if (uiType == "select") validateOptions(json.getJSONArray("options"), type, id)
            if (uiType == "button") require(id in KNOWN_ACTIONS) { "Неизвестное действие: $id" }

            return DeclarativeUiItem(
                id, title, type, uiType, value, min, max, step,
                json.optString("unit", ""), readOnly
            )
        }

        private fun validateOptions(options: JSONArray, type: String, id: String) {
            require(type == "string" || type == "integer") { "Недопустимый type select: $id" }
            require(options.length() > 0) { "Пустой список options: $id" }
            for (index in 0 until options.length()) {
                val option = options.get(index)
                val value = if (option is JSONObject) option.get("value") else option
                if (type == "string") require(value is String) { "Опция select не string: $id" }
                if (type == "integer") value.asLong("option: $id")
            }
        }

        private fun isSupported(uiType: String, type: String) = when (uiType) {
            "slider" -> type == "integer"
            "switch" -> type == "boolean"
            "select" -> type == "string" || type == "integer"
            "text" -> type == "string"
            "info" -> true
            "button" -> type == "action"
            else -> false
        }

        private val KNOWN_ACTIONS = setOf(
            "close", "reboot", "save", "ota_read", "ota_save", "ota_start"
        )
    }
}

data class DeclarativeUiSection(val id: String, val title: String, val items: List<DeclarativeUiItem>)

data class DeclarativeUiItem(
    val id: String,
    val title: String,
    val type: String,
    val uiType: String,
    val value: Any?,
    val min: Long?,
    val max: Long?,
    val step: Long?,
    val unit: String,
    val readOnly: Boolean
)

private fun String.requireText(field: String): String = also {
    require(isNotBlank()) { "Пустое поле $field" }
}

private fun JSONObject.optionalLong(name: String): Long? =
    if (!has(name) || isNull(name)) null else getLong(name)

private fun Any?.asLong(field: String): Long = when (this) {
    is Int -> toLong()
    is Long -> this
    is Number -> toLong().also { require(toDouble() == it.toDouble()) { "Не integer: $field" } }
    else -> error("Ожидалось integer: $field")
}
