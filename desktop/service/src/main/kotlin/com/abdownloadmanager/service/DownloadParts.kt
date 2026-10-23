package com.abdownloadmanager.service

import kotlinx.serialization.json.*

/** Strict, bounded wire selection for the read-only download.parts projection. */
internal fun decodePartsSelection(value: JsonElement?): List<Long> = value?.let {
    require(it is JsonArray && it.size <= EngineSession.MAX_PART_DOWNLOADS)
    it.map { id ->
        require(id is JsonPrimitive && id.isString && id.content.matches(Regex("0|[1-9][0-9]{0,18}")))
        id.content.toLong().also { number -> require(number >= 0) }
    }.also { ids -> require(ids.distinct().size == ids.size) }
}.orEmpty()
