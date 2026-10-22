package com.abdownloadmanager.service

import ir.amirab.downloader.downloaditem.IDownloadItem
import ir.amirab.downloader.part.Parts
import ir.amirab.downloader.part.RangedParts
import kotlinx.serialization.json.Json
import java.io.File

/** Read-only preflight before threads, stores or forgiving legacy readers boot.
 * Never choose/replay a .tmp transaction or repair a real profile implicitly.
 * Committed orphan part metadata can result from a partial keep-files removal;
 * it is preserved, not resurrected as a download. Orphan .tmp parts are also kept.
 */
internal fun validateStoredDownloads(database: File, json: Json) {
    fun entries(folder: File): List<File> {
        if (!folder.exists()) return emptyList()
        require(folder.isDirectory) { "Storage path is not a directory" }
        return folder.listFiles()?.toList() ?: error("Cannot enumerate download storage")
    }
    fun read(file: File): String {
        require(file.isFile && file.length() <= 2 * 1024 * 1024) { "Invalid or oversized stored download document" }
        return file.readText()
    }
    fun id(file: File): Long {
        val id = file.name.removeSuffix(".json").toLongOrNull()
        require(id != null && id >= 0 && id < Long.MAX_VALUE && file.name == "$id.json") { "Invalid download storage entry" }
        return id
    }
    val records = entries(database.resolve("downloadlist"))
    require(records.none { it.name.endsWith(".tmp") }) { "Unfinished download-list transaction; explicit recovery required" }
    val ids = records.filter { it.name != "last_id.txt" }.map { file ->
        val id = id(file)
        require(json.decodeFromString<IDownloadItem>(read(file)).id == id) { "Stored download ID differs from filename" }
        id
    }.toSet()
    val lastId = database.resolve("downloadlist/last_id.txt")
    if (lastId.exists()) {
        val high = json.decodeFromString<Long>(read(lastId))
        require(high >= (ids.maxOrNull() ?: -1) && high < Long.MAX_VALUE) { "Invalid or regressed download ID high-water mark" }
    } else require(ids.isEmpty()) { "Missing download ID high-water mark; explicit recovery required" }
    entries(database.resolve("queues")).forEach { file -> id(file); read(file) }
    entries(database.resolve("categories")).forEach { file ->
        require(file.name == "categories.json") { "Unexpected category transaction; explicit recovery required" }
        read(file)
    }
    entries(database.resolve("parts")).forEach { file ->
        if (file.name.endsWith(".json.tmp")) {
            val original = file.name.removeSuffix(".tmp")
            val partId = original.removeSuffix(".json").toLongOrNull()
            require(partId != null && partId >= 0 && original == "$partId.json" && partId !in ids && file.isFile) { "Unfinished part transaction; explicit recovery required" }
        } else {
            id(file)
            val parts = json.decodeFromString<Parts>(read(file))
            require(parts is RangedParts) { "Stored media-segment downloads need the HLS compatibility adapter" }
            require(parts.list.size <= 65536) { "Too many persisted parts" }
            val sorted = parts.list.sortedBy { it.from }
            sorted.forEachIndexed { index, part ->
                require(part.from >= 0 && part.current >= part.from &&
                    (part.to == null || (part.to!! < Long.MAX_VALUE && part.to!! >= part.from - 1 && part.current <= part.to!! + 1))) {
                    "Invalid persisted part range/progress"
                }
                if (index > 0) require(sorted[index - 1].to != null && sorted[index - 1].to!! < part.from) { "Overlapping persisted parts" }
            }
        }
    }
}
