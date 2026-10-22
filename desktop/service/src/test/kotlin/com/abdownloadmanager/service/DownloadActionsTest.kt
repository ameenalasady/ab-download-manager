package com.abdownloadmanager.service

import ir.amirab.downloader.destination.IncompleteFileUtil
import kotlinx.coroutines.runBlocking
import kotlinx.serialization.json.*
import java.nio.file.Files
import kotlin.test.*

class DownloadActionsTest {
    private fun request(op: String, id: String, name: String? = null, folder: String? = null) = buildJsonObject {
        put("op", op); put("id", id); name?.let { put("name", it) }; folder?.let { put("folder", it) }
    }
    @Test fun moveAndResetPreserveIdentityMembershipAndPayloadWithoutStarting() = runBlocking {
        for (incomplete in listOf(false, true)) {
            val root = Files.createTempDirectory("abdm-actions-")
            var lifecycle = ServiceLifecycle(root)
            try {
                var engine = lifecycle.start()
                val id = engine.command(buildJsonObject {
                    put("op", "add"); put("url", "http://127.0.0.1:12345/not-fetched"); put("name", "before.bin")
                }).getValue("id").jsonPrimitive.content
                engine.command(buildJsonObject { put("op", "queue.add"); put("queueId", "0"); put("id", id) })
                lifecycle.stop()
                Files.writeString(root.resolve("app-data/config/appSettings.json"), """{"appendExtensionToIncompleteDownloads":$incomplete}""")
                val db = root.resolve("app-data/config/download_db")
                val itemFile = db.resolve("downloadlist/$id.json")
                val item = Json.parseToJsonElement(Files.readString(itemFile)).jsonObject
                Files.writeString(itemFile, JsonObject(item + ("contentLength" to JsonPrimitive(1000))).toString())
                Files.writeString(db.resolve("parts/$id.json"), """{"type":"ranges","list":[{"from":0,"to":999,"current":250}]}""")
                val before = root.resolve("Downloads/before.bin").toFile()
                val source = if (incomplete) IncompleteFileUtil.addIncompleteIndicator(before, id.toLong()) else before
                source.writeText("disposable payload")
                lifecycle = ServiceLifecycle(root); engine = lifecycle.start()
                suspend fun row() = engine.snapshot().getValue("rows").jsonArray.single().jsonObject
                assertEquals("250", row().getValue("downloaded").jsonPrimitive.content)
                val folder = root.resolve("Downloads").toString()
                val occupied = root.resolve("Downloads/occupied.bin").toFile().apply { writeText("unmanaged") }
                assertFails { engine.command(request("download.move", id, occupied.name, folder)) }
                assertEquals("unmanaged", occupied.readText())
                assertFails { engine.command(request("download.move", id, "../escape", folder)) }
                assertFails { engine.command(request("download.move", id, "escape.bin", root.toString())) }
                val after = root.resolve("Downloads/after.bin").toFile()
                val target = if (incomplete) IncompleteFileUtil.addIncompleteIndicator(after, id.toLong()) else after
                // Occupied incomplete names are rejected even when the final name is free.
                val partialTarget = IncompleteFileUtil.addIncompleteIndicator(after, id.toLong())
                partialTarget.writeText("occupied partial")
                assertFails { engine.command(request("download.move", id, after.name, folder)) }
                assertEquals("occupied partial", partialTarget.readText()); partialTarget.delete()
                engine.command(request("download.move", id, after.name, folder))
                assertFalse(source.exists()); assertEquals("disposable payload", target.readText())
                assertEquals("after.bin", row().getValue("name").jsonPrimitive.content)
                assertEquals("250", row().getValue("downloaded").jsonPrimitive.content)
                engine.command(request("download.reset", id))
                assertEquals("0", row().getValue("downloaded").jsonPrimitive.content)
                assertEquals("queued", row().getValue("state").jsonPrimitive.content)
                assertEquals("disposable payload", target.readText()) // Reset does not itself truncate/start.
                assertTrue(engine.snapshot().getValue("queues").jsonArray.first().jsonObject.getValue("items").jsonArray.any { it.jsonPrimitive.content == id })
                lifecycle.stop(); lifecycle = ServiceLifecycle(root); engine = lifecycle.start()
                assertEquals(id, row().getValue("id").jsonPrimitive.content)
                assertEquals("after.bin", row().getValue("name").jsonPrimitive.content)
                assertEquals("0", row().getValue("downloaded").jsonPrimitive.content)
                assertEquals("disposable payload", target.readText())
            } finally { lifecycle.stop(); root.toFile().deleteRecursively() }
        }
    }
}
