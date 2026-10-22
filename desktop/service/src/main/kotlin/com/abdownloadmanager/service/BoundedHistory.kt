package com.abdownloadmanager.service

import kotlinx.serialization.json.*
import java.security.MessageDigest
import java.util.UUID

/** Read-only, bounded summary windows/deltas. Cursors bind membership/order and this
 * engine instance, not a mutable numeric offset across restarts. No retained per-client
 * histories, commands, details/credentials, or unbounded removal lists. A membership
 * change invalidates cursors explicitly; callers resnapshot reads, never commands.
 * Query indexing remains O(history); this is not the incremental engine index yet.
 */
internal class BoundedHistory {
    private val epoch = UUID.randomUUID().toString()
    private var generation = 0L
    private var order = emptyList<String>()
    private fun hash(row: JsonObject) = MessageDigest.getInstance("SHA-256").digest(row.toString().toByteArray(Charsets.UTF_8))
        .joinToString("") { "%02x".format(it) }

    fun query(rows: List<JsonObject>, request: JsonObject): JsonObject {
        require(rows.size <= 50_000) { "History exceeds bounded-query fixture limit" }
        val ids = rows.map { it.getValue("id").jsonPrimitive.content }
        require(ids.distinct().size == ids.size) { "Duplicate history ID" }
        if (ids != order) { generation++; order = ids }
        val version = "$epoch-$generation"
        val size = request["limit"]?.jsonPrimitive?.int ?: 100
        require(size in 1..200) { "History page limit must be 1–200" }
        val cursor = request["cursor"]?.takeUnless { it == JsonNull }?.jsonPrimitive?.content
        val offset = if (cursor == null) 0 else {
            require(cursor.substringBefore(':') == version) { "History cursor stale; resnapshot read-only history" }
            cursor.substringAfter(':', "").toIntOrNull()?.also { require(it in 0..rows.size) } ?: error("Invalid history cursor")
        }
        val known = request["known"]?.jsonObject.orEmpty()
        require(known.size <= 200 && known.all { (id, digest) ->
            id.toLongOrNull()?.let { it >= 0 && it.toString() == id } == true && digest is JsonPrimitive && digest.isString && digest.content.matches(Regex("[a-f0-9]{64}"))
        }) { "Invalid bounded history delta baseline" }
        val window = rows.subList(offset, minOf(offset + size, rows.size))
        val entries = window.map { row -> row to hash(row) }
        val hashes = entries.associate { (row, hash) -> row.getValue("id").jsonPrimitive.content to JsonPrimitive(hash) }
        val changed = entries.filter { (row, hash) -> known[row.getValue("id").jsonPrimitive.content]?.jsonPrimitive?.content != hash }.map { it.first }
        val report = buildJsonObject {
            put("version", version); put("total", rows.size.toString()); put("cursor", "$version:$offset")
            put("nextCursor", if (offset + window.size < rows.size) JsonPrimitive("$version:${offset + window.size}") else JsonNull)
            put("rows", JsonArray(changed)); put("hashes", JsonObject(hashes))
            put("order", JsonArray(window.map { it.getValue("id") }))
            put("removed", JsonArray((known.keys - hashes.keys).map(::JsonPrimitive)))
            put("detailsIncluded", false); put("atomicAcrossPages", false)
        }
        require(report.toString().toByteArray(Charsets.UTF_8).size <= 256 * 1024) { "History page exceeds summary byte bound" }
        return report
    }
}
