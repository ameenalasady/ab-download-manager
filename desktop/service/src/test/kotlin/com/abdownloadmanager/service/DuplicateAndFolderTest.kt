package com.abdownloadmanager.service

import kotlinx.coroutines.runBlocking
import kotlinx.serialization.json.*
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.attribute.PosixFilePermissions
import java.util.UUID
import kotlin.test.*

class DuplicateAndFolderTest {
    private val url = "http://127.0.0.1:12345/fixture"
    private fun add(name: String, policy: String? = null) = buildJsonObject {
        put("op", "add"); put("url", url); put("name", name); policy?.let { put("duplicatePolicy", it) }
    }
    private suspend fun rows(engine: EngineSession) = engine.snapshot().getValue("rows").jsonArray.map { it.jsonObject }
    private fun private(path: Path) = Files.setPosixFilePermissions(path, PosixFilePermissions.fromString(if (Files.isDirectory(path)) "rwx------" else "rw-------"))

    @Test fun overwriteReplacesOnlyTheSameDestinationWithItsFileAndMembership() = runBlocking {
        val root = Files.createTempDirectory("abdm-duplicate-replace-")
        val lifecycle = ServiceLifecycle(root)
        try {
            val engine = lifecycle.start()
            val first = engine.command(add("same.bin")).getValue("id").jsonPrimitive.content
            val other = engine.command(add("other.bin")).getValue("id").jsonPrimitive.content
            engine.command(buildJsonObject { put("op", "queue.add"); put("queueId", "0"); put("id", first) })
            val category = engine.snapshot().getValue("categories").jsonArray.first().jsonObject.getValue("id").jsonPrimitive.content
            engine.command(buildJsonObject { put("op", "category.assign"); put("categoryId", category); put("id", first) })
            assertTrue(engine.snapshot().getValue("categories").jsonArray.any { c ->
                first in c.jsonObject.getValue("items").jsonArray.map { it.jsonPrimitive.content } })
            val payload = root.resolve("Downloads/same.bin").toFile().apply { writeText("old payload") }
            val untouched = root.resolve("Downloads/other.bin").toFile().apply { writeText("keep") }
            // Default stays the original AddNumbered: nothing is replaced implicitly.
            val numbered = engine.command(add("same.bin")).getValue("id").jsonPrimitive.content
            assertEquals("same_1.bin", rows(engine).single { it.getValue("id").jsonPrimitive.content == numbered }.getValue("name").jsonPrimitive.content)
            assertFails { engine.command(add("same.bin", "replace-everything")) }
            val replacement = engine.command(add("same.bin", "overwrite")).getValue("id").jsonPrimitive.content
            val ids = rows(engine).map { it.getValue("id").jsonPrimitive.content }
            assertFalse(first in ids); assertTrue(replacement in ids && numbered in ids && other in ids)
            assertEquals("same.bin", rows(engine).single { it.getValue("id").jsonPrimitive.content == replacement }.getValue("name").jsonPrimitive.content)
            assertFalse(payload.exists()) // The replaced record's file goes with it, as in the original.
            assertEquals("keep", untouched.readText())
            assertTrue(engine.snapshot().getValue("queues").jsonArray.all { q ->
                first !in q.jsonObject.getValue("items").jsonArray.map { it.jsonPrimitive.content } })
            assertTrue(engine.snapshot().getValue("categories").jsonArray.all { c ->
                first !in c.jsonObject.getValue("items").jsonArray.map { it.jsonPrimitive.content } })
        } finally { lifecycle.stop(); root.toFile().deleteRecursively() }
    }

    @Test fun overwriteNeverDeletesAnUnmanagedFileOrAnotherDestinationWithTheSameUrl() = runBlocking {
        val root = Files.createTempDirectory("abdm-duplicate-unmanaged-")
        val lifecycle = ServiceLifecycle(root)
        try {
            val engine = lifecycle.start()
            val other = engine.command(add("other.bin")).getValue("id").jsonPrimitive.content
            val unmanaged = root.resolve("Downloads/unmanaged.bin")
            Files.writeString(unmanaged, "not-owned-by-a-record")
            val added = engine.command(add("unmanaged.bin", "overwrite")).getValue("id").jsonPrimitive.content
            assertEquals("not-owned-by-a-record", Files.readString(unmanaged))
            assertTrue(rows(engine).any { it.getValue("id").jsonPrimitive.content == other })
            assertEquals("unmanaged_1.bin", rows(engine).single { it.getValue("id").jsonPrimitive.content == added }.getValue("name").jsonPrimitive.content)
        } finally { lifecycle.stop(); root.toFile().deleteRecursively() }
    }

    @Test fun duplicateActionIsAValidatedFrontendPreference() = runBlocking {
        val root = Files.createTempDirectory("abdm-duplicate-action-")
        val lifecycle = ServiceLifecycle(root)
        try {
            val engine = lifecycle.start()
            assertTrue("download.replace" in EngineSession.capabilities && "settings.duplicates" in EngineSession.capabilities)
            assertEquals("ask", engine.snapshot().getValue("settings").jsonObject.getValue("qtDuplicateAction").jsonPrimitive.content)
            engine.command(buildJsonObject { put("op", "settings.update"); put("settings", buildJsonObject { put("qtDuplicateAction", "existing") }) })
            assertEquals("existing", engine.snapshot().getValue("settings").jsonObject.getValue("qtDuplicateAction").jsonPrimitive.content)
            for (invalid in listOf(JsonPrimitive("overwrite"), JsonPrimitive(true)))
                assertFails { engine.command(buildJsonObject { put("op", "settings.update"); put("settings", buildJsonObject { put("qtDuplicateAction", invalid) }) }) }
            assertEquals("existing", engine.snapshot().getValue("settings").jsonObject.getValue("qtDuplicateAction").jsonPrimitive.content)
        } finally { lifecycle.stop(); root.toFile().deleteRecursively() }
    }

    @Test fun defaultFolderIsHonoredOnlyInsideTheRuntimeRoots() = runBlocking {
        val root = Files.createTempDirectory("abdm-default-folder-")
        val profile = Files.createDirectory(root.resolve("profile")); private(profile)
        val downloads = Files.createDirectory(root.resolve("downloads"))
        val nested = Files.createDirectory(downloads.resolve("videos"))
        val outside = Files.createDirectory(root.resolve("outside"))
        val id = UUID.randomUUID().toString()
        Files.writeString(profile.resolve("qt-runtime-profile.json"), """{"version":1,"profileId":"$id"}"""); private(profile.resolve("qt-runtime-profile.json"))
        val file = profile.resolve("qt-runtime-policy.json").also {
            Files.writeString(it, buildJsonObject {
                put("version", 1); put("profileId", id); put("profile", profile.toString()); put("destination", downloads.toString())
                put("destinationRoots", JsonArray(listOf(JsonPrimitive(downloads.toString())))); put("redirects", true)
                put("proxy", buildJsonObject { put("type", "direct") })
            }.toString()); private(it)
        }
        val policy = RuntimePolicy.load(file)
        val lifecycle = ServiceLifecycle(policy.profile, policy)
        fun settings(folder: String) = buildJsonObject { put("op", "settings.update"); put("settings", buildJsonObject { put("defaultDownloadFolder", folder) }) }
        suspend fun destination(engine: EngineSession) = engine.snapshot().getValue("runtime").jsonObject.getValue("destination").jsonPrimitive.content
        try {
            var engine = lifecycle.start()
            assertEquals(downloads.toString(), destination(engine))
            for (invalid in listOf(outside.toString(), downloads.resolve("missing").toString(), "relative"))
                assertFails { engine.command(settings(invalid)) }
            assertEquals(downloads.toString(), destination(engine))
            assertFails {
                engine.command(buildJsonObject {
                    put("op", "settings.update")
                    put("settings", buildJsonObject { put("defaultDownloadFolder", nested.toString()); put("qtDuplicateAction", "overwrite") })
                })
            }
            assertEquals(downloads.toString(), destination(engine)) // Invalid patches cannot partly change the folder.
            engine.command(settings(nested.toString()))
            assertEquals(nested.toString(), destination(engine))
            val added = engine.command(buildJsonObject { put("op", "add"); put("url", "https://example.invalid/a.bin"); put("name", "a.bin") })
            assertEquals(nested.toString(), rows(engine).single { it.getValue("id") == added.getValue("id") }
                .getValue("details").jsonObject.getValue("folder").jsonPrimitive.content)
            lifecycle.stop(); engine = lifecycle.start()
            assertEquals(nested.toString(), destination(engine)) // Persisted in the original key.
            // A folder that disappears falls back to the runtime destination; empty resets.
            Files.delete(nested)
            assertEquals(downloads.toString(), destination(engine))
            engine.command(settings(""))
            assertEquals(downloads.toString(), destination(engine))
            assertFalse(policy.profile.resolve("app-data/config/appSettings.json").toFile().readText().contains("videos"))
        } finally { lifecycle.stop(); root.toFile().deleteRecursively() }
    }
}
