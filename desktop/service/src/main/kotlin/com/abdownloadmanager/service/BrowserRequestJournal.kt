package com.abdownloadmanager.service

import kotlinx.serialization.json.*
import java.nio.ByteBuffer
import java.nio.channels.FileChannel
import java.nio.file.Files
import java.nio.file.LinkOption.NOFOLLOW_LINKS
import java.nio.file.Path
import java.nio.file.StandardCopyOption.ATOMIC_MOVE
import java.nio.file.StandardCopyOption.REPLACE_EXISTING
import java.nio.file.StandardOpenOption.*
import java.nio.file.attribute.PosixFilePermissions

/** Service-lease-owned review journal. Never contains executable commands. A reserved
 * request is an UNKNOWN outcome after restart, never an instruction to retry Add.
 * Interrupted journal writes fail closed and retain both files for explicit inspection.
 * File data is forced; directory/power-loss durability is NOT claimed.
 */
internal class BrowserRequestJournal(private val file: Path) {
    private val temporary = file.resolveSibling(file.fileName.toString() + ".tmp")
    private val permissions = PosixFilePermissions.fromString("rw-------")
    private fun checkFile(path: Path) {
        RuntimePolicy.noSymlinkAncestors(path)
        require(Files.isRegularFile(path, NOFOLLOW_LINKS) && Files.size(path) <= 2 * 1024 * 1024 &&
            Files.getPosixFilePermissions(path, NOFOLLOW_LINKS) == permissions &&
            (Files.getAttribute(path, "unix:nlink", NOFOLLOW_LINKS) as Number).toLong() == 1L &&
            Files.getOwner(path) == file.fileSystem.userPrincipalLookupService.lookupPrincipalByName(System.getProperty("user.name"))) {
            "Invalid browser review journal; explicit inspection required"
        }
    }
    fun load(): List<BrowserRequest> {
        RuntimePolicy.noSymlinkAncestors(file)
        require(!Files.exists(temporary, NOFOLLOW_LINKS)) { "Unfinished browser review journal; explicit recovery required" }
        if (!Files.exists(file, NOFOLLOW_LINKS)) return emptyList()
        checkFile(file)
        val raw = Files.readString(file)
        val doc = Json.parseToJsonElement(raw).jsonObject
        require(raw == doc.toString()) { "Noncanonical or duplicate-key browser review journal; explicit inspection required" }
        require(doc.keys == setOf("version", "requests") && doc.getValue("version").jsonPrimitive.int == 1)
        fun text(value: JsonElement): String { require(value is JsonPrimitive && value.isString); return value.content }
        return doc.getValue("requests").jsonArray.map { value ->
            val r = value.jsonObject
            require(r.keys == setOf("id", "receivedAt", "source", "outcomeUnknown", "items"))
            val items = r.getValue("items").jsonArray.map { item ->
                val i = item.jsonObject
                require(i.keys == setOf("url", "name", "downloadPage", "credentials"))
                BrowserItem(text(i.getValue("url")), text(i.getValue("name")),
                    i.getValue("downloadPage").takeUnless { it == JsonNull }?.let(::text),
                    i.getValue("credentials").takeUnless { it == JsonNull }?.jsonObject)
            }
            BrowserRequest(text(r.getValue("id")), items, text(r.getValue("receivedAt")).toLong(),
                text(r.getValue("source")), r.getValue("outcomeUnknown").jsonPrimitive.boolean)
        }
    }
    fun save(requests: Collection<BrowserRequest>) {
        RuntimePolicy.noSymlinkAncestors(file)
        require(!Files.exists(temporary, NOFOLLOW_LINKS)) { "Unfinished browser review journal; no automatic retry" }
        if (Files.exists(file, NOFOLLOW_LINKS)) checkFile(file)
        val bytes = buildJsonObject {
            put("version", 1)
            put("requests", JsonArray(requests.map { r -> buildJsonObject {
                put("id", r.id); put("receivedAt", r.receivedAt.toString()); put("source", r.source); put("outcomeUnknown", r.outcomeUnknown)
                put("items", JsonArray(r.items.map { i -> buildJsonObject {
                    put("url", i.url); put("name", i.name); put("downloadPage", i.downloadPage?.let(::JsonPrimitive) ?: JsonNull)
                    put("credentials", i.credentials ?: JsonNull)
                } }))
            } }))
        }.toString().toByteArray(Charsets.UTF_8)
        require(bytes.size <= 2 * 1024 * 1024) { "Browser review journal is full" }
        FileChannel.open(temporary, setOf(CREATE_NEW, WRITE, NOFOLLOW_LINKS), PosixFilePermissions.asFileAttribute(permissions)).use { channel ->
            val buffer = ByteBuffer.wrap(bytes)
            while (buffer.hasRemaining()) channel.write(buffer)
            channel.force(true)
        }
        // No non-atomic fallback; failed publication retains .tmp and blocks more writes.
        Files.move(temporary, file, ATOMIC_MOVE, REPLACE_EXISTING)
    }
}
