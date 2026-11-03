package com.abdownloadmanager.service

import kotlinx.coroutines.*
import kotlinx.serialization.json.*
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.*

class DeletedFileTrackingTest {
    private suspend fun add(engine: EngineSession, name: String) = engine.command(buildJsonObject {
        put("op", "add"); put("url", "http://127.0.0.1:12345/not-fetched"); put("name", name)
    }).getValue("id").jsonPrimitive.content

    private fun complete(root: Path, id: String, name: String) {
        val record = root.resolve("app-data/config/download_db/downloadlist/$id.json")
        val item = Json.parseToJsonElement(Files.readString(record)).jsonObject
        Files.writeString(root.resolve("Downloads/$name"), "payload")
        Files.writeString(record, JsonObject(item + mapOf("status" to JsonPrimitive("Completed"), "contentLength" to JsonPrimitive(7))).toString())
    }

    private suspend fun EngineSession.ids() = snapshot().getValue("rows").jsonArray.map { it.jsonObject.getValue("id").jsonPrimitive.content }.toSet()

    private suspend fun EngineSession.track(enabled: Boolean) = command(buildJsonObject {
        put("op", "settings.update"); put("settings", buildJsonObject { put("trackDeletedFilesOnDisk", enabled) })
    })

    @Test fun deletedCompletedFilesLeaveTheListOnlyWhileTracking() = runBlocking {
        val root = Files.createTempDirectory("abdm-deleted-files-")
        val lifecycle = ServiceLifecycle(root)
        try {
            var engine = lifecycle.start()
            assertFalse(engine.snapshot().getValue("settings").jsonObject.getValue("trackDeletedFilesOnDisk").jsonPrimitive.boolean)
            val gone = add(engine, "gone.bin"); val kept = add(engine, "kept.bin")
            val renamed = add(engine, "renamed.bin"); val pending = add(engine, "pending.bin")
            lifecycle.stop()
            for ((id, name) in listOf(gone to "gone.bin", kept to "kept.bin", renamed to "renamed.bin")) complete(root, id, name)
            engine = lifecycle.start()

            // Off by default (the original's default): a deleted file keeps its row.
            Files.delete(root.resolve("Downloads/gone.bin"))
            delay(1500)
            assertEquals(setOf(gone, kept, renamed, pending), engine.ids())

            // Turning it on catches up immediately; a never-started download has no file yet and stays.
            engine.track(true)
            withTimeout(5_000) { while (gone in engine.ids()) delay(50) }
            assertEquals(setOf(kept, renamed, pending), engine.ids())

            // While on, a rename/move away is noticed by the folder watch, well before the slow re-check.
            Files.move(root.resolve("Downloads/renamed.bin"), root.resolve("Downloads/elsewhere.bin"))
            withTimeout(EngineSession.DELETED_FILES_RECHECK_MS / 2) { while (renamed in engine.ids()) delay(50) }
            assertEquals(setOf(kept, pending), engine.ids())
            assertEquals("payload", Files.readString(root.resolve("Downloads/elsewhere.bin"))) // Records only; files untouched.

            // Persisted: the removal survives a restart and tracking resumes on boot.
            lifecycle.stop(); engine = lifecycle.start()
            assertEquals(setOf(kept, pending), engine.ids())
            Files.delete(root.resolve("Downloads/kept.bin"))
            withTimeout(5_000) { while (kept in engine.ids()) delay(50) }

            engine.track(false)
            val again = add(engine, "again.bin")
            lifecycle.stop(); complete(root, again, "again.bin"); engine = lifecycle.start()
            Files.delete(root.resolve("Downloads/again.bin"))
            delay(1500)
            assertTrue(again in engine.ids())
        } finally { lifecycle.stop(); root.toFile().deleteRecursively() }
    }

    @Test fun aMissingFolderIsTreatedAsUnmountedNotDeleted() = runBlocking {
        val root = Files.createTempDirectory("abdm-deleted-folder-")
        val lifecycle = ServiceLifecycle(root)
        try {
            var engine = lifecycle.start()
            val id = add(engine, "file.bin")
            engine.track(true)
            lifecycle.stop()
            complete(root, id, "file.bin")
            val record = root.resolve("app-data/config/download_db/downloadlist/$id.json")
            val sub = root.resolve("Downloads/sub").also { Files.createDirectories(it) }
            Files.move(root.resolve("Downloads/file.bin"), sub.resolve("file.bin"))
            val item = Json.parseToJsonElement(Files.readString(record)).jsonObject
            Files.writeString(record, JsonObject(item + ("folder" to JsonPrimitive(sub.toString()))).toString())
            sub.toFile().deleteRecursively()
            engine = lifecycle.start()
            delay(1500)
            assertEquals(setOf(id), engine.ids())
        } finally { lifecycle.stop(); root.toFile().deleteRecursively() }
    }
}
