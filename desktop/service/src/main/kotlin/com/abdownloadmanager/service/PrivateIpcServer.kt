package com.abdownloadmanager.service

import kotlinx.coroutines.runBlocking
import kotlinx.serialization.json.*
import java.io.DataInputStream
import java.io.DataOutputStream
import java.net.StandardProtocolFamily
import java.net.UnixDomainSocketAddress
import java.nio.channels.Channels
import java.nio.channels.ServerSocketChannel
import java.nio.channels.SocketChannel
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.StandardCopyOption.*
import java.nio.file.attribute.PosixFilePermissions
import java.security.MessageDigest
import java.security.SecureRandom
import java.util.Base64
import java.util.UUID
import java.util.concurrent.*

/** v1 pull protocol: length-prefixed UTF-8 JSON, bounded request/reply, one outstanding
 * request per client. Pull snapshots naturally coalesce updates and bound backpressure.
 * Clients resnapshot after reconnect; never automatically replay a command across epochs.
 */
class PrivateIpcServer(private val profile: Path, private val engine: EngineSession) : AutoCloseable {
    companion object { const val MAX_FRAME = 2 * 1024 * 1024; const val VERSION = 1 }
    private val json = Json { ignoreUnknownKeys = false }
    private val epoch = UUID.randomUUID().toString()
    private val token = Base64.getUrlEncoder().withoutPadding().encodeToString(ByteArray(32).also { SecureRandom().nextBytes(it) })
    private val directory = Files.createTempDirectory("abdm-ipc-", PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rwx------")))
    private val socket = directory.resolve("service.sock")
    private val server = try { ServerSocketChannel.open(StandardProtocolFamily.UNIX) }
        catch (e: Throwable) { Files.deleteIfExists(directory); throw e }
    private val connections = ConcurrentHashMap.newKeySet<SocketChannel>()
    private val deadlines = Executors.newSingleThreadScheduledExecutor()
    private val workers = ThreadPoolExecutor(4, 4, 0, TimeUnit.SECONDS, SynchronousQueue())
    private val gate = Any()
    private val replies = object : LinkedHashMap<String, Pair<JsonObject, JsonObject>>() {
        override fun removeEldestEntry(eldest: MutableMap.MutableEntry<String, Pair<JsonObject, JsonObject>>?) = size > 1024
    }
    @Volatile private var closed = false

    fun serve() {
        server.bind(UnixDomainSocketAddress.of(socket))
        Files.setPosixFilePermissions(socket, PosixFilePermissions.fromString("rw-------"))
        val descriptor = buildJsonObject {
            put("version", VERSION); put("epoch", epoch); put("socket", socket.toString()); put("token", token)
        }
        val temp = profile.resolve(".qt-service.json.tmp")
        // No permissive creation window, including under a permissive inherited umask.
        Files.deleteIfExists(temp)
        Files.createFile(temp, PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rw-------")))
        Files.writeString(temp, descriptor.toString())
        Files.move(temp, profile.resolve("qt-service.json"), ATOMIC_MOVE, REPLACE_EXISTING)
        println("SERVICE_READY version=$VERSION") // Never log credentials or URLs.
        while (!closed) {
            val client = try { server.accept() } catch (e: Exception) { if (closed) break else throw e }
            connections.add(client)
            try { workers.execute { handle(client) } }
            catch (_: RejectedExecutionException) { connections.remove(client); client.close() }
        }
    }

    private fun handle(client: SocketChannel) {
        try {
            client.use {
                val input = DataInputStream(Channels.newInputStream(client))
                val output = DataOutputStream(Channels.newOutputStream(client))
                var authenticated = false
                var clientId = ""
                var frontend = false
                while (!closed) {
                    // UNIX SocketChannel has no SO_TIMEOUT. Close stalled readers AND writers.
                    val timeout = deadlines.schedule({ runCatching { client.close() } }, 10, TimeUnit.SECONDS)
                    try {
                        val size = input.readInt()
                        require(size in 1..MAX_FRAME) { "Invalid frame length" }
                        val request = json.parseToJsonElement(Charsets.UTF_8.newDecoder().decode(java.nio.ByteBuffer.wrap(
                            input.readNBytes(size).also { require(it.size == size) })).toString()).jsonObject
                        val response = if (!authenticated) {
                            require(request["type"]?.jsonPrimitive?.content == "hello")
                            require(request["version"]?.jsonPrimitive?.int == VERSION)
                            val supplied = request["token"]?.jsonPrimitive?.content.orEmpty()
                            require(MessageDigest.isEqual(supplied.toByteArray(), token.toByteArray()))
                            clientId = request["clientId"]?.jsonPrimitive?.content.orEmpty()
                            require(clientId.matches(Regex("[a-zA-Z0-9-]{1,80}")))
                            // Optional: a window that shows browser requests (helpers/status omit it).
                            frontend = request["role"]?.jsonPrimitive?.contentOrNull == "frontend"
                            authenticated = true
                            buildJsonObject {
                                put("type", "hello"); put("version", VERSION); put("epoch", epoch)
                                put("capabilities", JsonArray(EngineSession.capabilities.map(::JsonPrimitive)))
                                put("maxFrame", MAX_FRAME); put("deduplicationWindow", 1024)
                            }
                        } else synchronized(gate) { dispatch(clientId, request, frontend) }
                        var bytes = response.toString().toByteArray(Charsets.UTF_8)
                        if (bytes.size > MAX_FRAME) bytes = buildJsonObject {
                            put("type", "serviceError"); put("epoch", epoch); put("code", "snapshot-too-large")
                            put("error", "History exceeds the v1 full-snapshot bound; this frontend must use the separate bounded history API")
                        }.toString().toByteArray(Charsets.UTF_8)
                        output.writeInt(bytes.size); output.write(bytes); output.flush()
                        if (authenticated && request["op"]?.jsonPrimitive?.content == "service.shutdown" &&
                            response["result"]?.jsonObject?.get("ok")?.jsonPrimitive?.booleanOrNull == true) {
                            // Stop accepting only AFTER the acknowledgement. Main drains workers
                            // and every writer before removing credentials/releasing the lease.
                            closed = true; server.close(); break
                        }
                    } finally { timeout.cancel(false) }
                }
            }
        } catch (_: Exception) {
            // Invalid auth, protocol, EOF and timeout all close without leaking credentials/details.
        } finally { connections.remove(client); runCatching { client.close() } }
    }

    private fun dispatch(clientId: String, request: JsonObject, frontend: Boolean): JsonObject {
        if (request["type"]?.jsonPrimitive?.content in setOf("history.page", "history.delta")) {
            // Read-only extension. No command IDs/dedup replay and no full metadata
            // snapshot or credential-bearing details embedded in bounded history frames.
            return try { buildJsonObject {
                put("type", "history.page"); put("epoch", epoch); put("history", runBlocking { engine.historyPage(request) })
            } } catch (_: Exception) { buildJsonObject {
                put("type", "historyError"); put("epoch", epoch); put("code", "history-query-rejected")
                put("error", "Invalid, stale or oversized history query; resnapshot read-only history. No command was applied.")
            } }
        }
        if (request["type"]?.jsonPrimitive?.content == "snapshot") {
            if (frontend) engine.frontendSeen(request["active"]?.jsonPrimitive?.booleanOrNull == true)
            // Optional download.parts projection: strict stable string IDs and bounded
            // selection. Read-only polling; not a command and not part of dedup/replay.
            val selection = decodePartsSelection(request["partsFor"])
            return buildJsonObject { put("type", "snapshot"); put("epoch", epoch); put("snapshot", runBlocking { engine.snapshot(selection) }) }
        }
        require(request["type"]?.jsonPrimitive?.content == "command")
        require(request["epoch"]?.jsonPrimitive?.content == epoch) { "Stale service epoch" }
        val id = request["commandId"]?.jsonPrimitive?.content.orEmpty()
        require(id.matches(Regex("[a-zA-Z0-9-]{1,80}")))
        val key = "$clientId:$id"
        replies[key]?.let { (original, reply) ->
            require(original == request) { "Command ID reused for a different operation" }
            return reply
        }
        val reply = try {
            if (request["op"]?.jsonPrimitive?.content == "service.shutdown") buildJsonObject { put("ok", true) }
            else runBlocking { engine.command(request) }
        }
        catch (e: Exception) { buildJsonObject { put("ok", false)
            put("code", when {
                e is DownloadAddedButCategoryFailed -> "add-partially-applied"
                e is DownloadForgetPartiallyApplied || e is DownloadDeletePartiallyApplied -> "remove-partially-applied"
                e is DownloadEditPartiallyApplied -> "edit-partially-applied"
                e is QueueDeletePartiallyApplied -> "queue-delete-partially-applied"
                e is BrowserAddPartiallyApplied -> "browser-add-partially-applied"
                e is DownloadReplacePartiallyApplied -> "replace-partially-applied"
                request["op"]?.jsonPrimitive?.content !in EngineSession.capabilities -> "unsupported-command"
                else -> "command-rejected"
            })
            if (e is DownloadAddedButCategoryFailed) put("id", e.downloadId.toString())
            if (e is DownloadForgetPartiallyApplied) put("id", e.downloadId.toString())
            if (e is DownloadDeletePartiallyApplied) put("id", e.downloadId.toString())
            if (e is DownloadEditPartiallyApplied) put("id", e.downloadId.toString())
            if (e is DownloadReplacePartiallyApplied) put("id", e.downloadId.toString())
            if (e is QueueDeletePartiallyApplied) put("queueId", e.queueId.toString())
            put("error", e.message?.take(240) ?: "Command failed") } }
        val correlated = buildJsonObject {
            put("type", "reply"); put("epoch", epoch); put("commandId", id); put("result", reply)
        }
        replies[key] = request to correlated
        return correlated
    }

    @Synchronized override fun close() {
        closed = true
        server.close()
        connections.forEach { runCatching { it.close() } }
        workers.shutdown()
        check(workers.awaitTermination(15, TimeUnit.SECONDS)) { "IPC commands did not drain; retaining profile lock" }
        deadlines.shutdownNow()
        check(deadlines.awaitTermination(5, TimeUnit.SECONDS)) { "IPC deadlines did not drain" }
        // Only remove this epoch's publication, never a replacement descriptor.
        val descriptor = profile.resolve("qt-service.json")
        if (Files.exists(descriptor) && runCatching { json.parseToJsonElement(Files.readString(descriptor)).jsonObject["epoch"]?.jsonPrimitive?.content == epoch }.getOrDefault(false))
            Files.delete(descriptor)
        Files.deleteIfExists(profile.resolve(".qt-service.json.tmp"))
        Files.deleteIfExists(socket); Files.deleteIfExists(directory)
    }
}
