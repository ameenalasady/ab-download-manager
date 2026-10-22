package com.abdownloadmanager.service

import com.abdownloadmanager.shared.util.category.Category
import com.abdownloadmanager.shared.util.category.StandardCategoryDefaults
import kotlinx.coroutines.*
import kotlinx.serialization.builtins.ListSerializer
import kotlinx.serialization.json.*
import java.nio.file.Files
import kotlin.test.*

class CategoryRepositoryTest {
    private fun command(op: String, vararg args: Pair<String, JsonElement>) = JsonObject(mapOf("op" to JsonPrimitive(op), *args))
    private fun draft(name: String, types: List<String> = listOf("zip"), urls: List<String> = emptyList()) = arrayOf(
        "name" to JsonPrimitive(name), "acceptedFileTypes" to JsonArray(types.map(::JsonPrimitive)),
        "acceptedUrlPatterns" to JsonArray(urls.map(::JsonPrimitive)))
    private suspend fun categories(engine: EngineSession) = engine.snapshot().getValue("categories").jsonArray.map { it.jsonObject }
    private suspend fun add(engine: EngineSession, name: String, category: String = "auto") = engine.command(command("add",
        "url" to JsonPrimitive("http://127.0.0.1:12345/files/test"), "name" to JsonPrimitive(name),
        "categoryId" to JsonPrimitive(category))).getValue("id").jsonPrimitive.content

    @Test fun sharedOriginalDefaultsAndSpecificityPolicyPersistAcrossImmediateShutdown() = runBlocking {
        val root = Files.createTempDirectory("abdm-category-test-")
        val lifecycle = ServiceLifecycle(root)
        try {
            var engine = lifecycle.start()
            val original = categories(engine)
            assertEquals(listOf("Compressed", "Programs", "Videos", "Music", "Pictures", "Documents"), original.map { it.getValue("name").jsonPrimitive.content })
            val id = engine.command(command("category.create", *draft("Fixture ZIP", urls = listOf("http://127.0.0.1:*/files/*"))))
                .getValue("categoryId").jsonPrimitive.content
            assertEquals("101", id)
            val auto = add(engine, "UPPER.ZIP")
            val none = add(engine, "none.zip", "none")
            val fixed = add(engine, "fixed.zip", "0")
            fun JsonObject.members() = getValue("items").jsonArray.map { it.jsonPrimitive.content }
            var all = categories(engine)
            assertEquals(listOf(auto), all.last().members()) // URL-specific category wins over default.
            assertEquals(listOf(fixed), all.first().members())
            assertTrue(all.none { none in it.members() })
            val before = root.resolve("app-data/config/download_db/categories/categories.json").toFile().readText()
            assertFails { engine.command(command("category.update", "categoryId" to JsonPrimitive(id), *draft("Not applied", types = listOf("bad/type")))) }
            assertEquals(before, root.resolve("app-data/config/download_db/categories/categories.json").toFile().readText())
            assertFails { engine.command(command("category.update", "categoryId" to JsonPrimitive(id), *draft("Escape"), "path" to JsonPrimitive("/tmp"))) }
            val rowsBefore = engine.snapshot().getValue("rows").jsonArray.size
            assertFails { add(engine, "invalid.zip", "999") }
            assertEquals(rowsBefore, engine.snapshot().getValue("rows").jsonArray.size)
            engine.command(command("category.assign", "categoryId" to JsonPrimitive("0"), "id" to JsonPrimitive(auto)))
            all = categories(engine)
            assertTrue(all.last().members().isEmpty()); assertEquals(setOf(fixed, auto), all.first().members().toSet())
            engine.command(command("category.update", "categoryId" to JsonPrimitive(id), *draft("Renamed", types = listOf("bin"))))
            engine.command(command("category.move", "categoryId" to JsonPrimitive(id), "direction" to JsonPrimitive(-1)))
            lifecycle.stop() // No autosave sleep.
            fun persisted() = root.toFile().walkTopDown().filter { it.isFile && it.extension == "json" }
                .associate { it.relativeTo(root.toFile()).path to (it.readText() to it.lastModified()) }
            val settled = persisted()
            ProfileLease.acquire(root.resolve("app-data/config/app.lock")).use { delay(900); assertEquals(settled, persisted()) }
            engine = lifecycle.start()
            assertEquals("Renamed", categories(engine)[5].getValue("name").jsonPrimitive.content)
            engine.command(command("category.delete", "categoryId" to JsonPrimitive("0")))
            assertEquals(rowsBefore, engine.snapshot().getValue("rows").jsonArray.size) // Category deletion never removes records/files.
            assertTrue(categories(engine).none { it["id"]?.jsonPrimitive?.content == "0" })
            lifecycle.stop()
        } finally { lifecycle.stop(); root.toFile().deleteRecursively() }
    }

    @Test fun categoryWriteFailureReportsCommittedAddWithoutLosingItsStableId() = runBlocking {
        val root = Files.createTempDirectory("abdm-category-write-failure-test-")
        val lifecycle = ServiceLifecycle(root)
        val blockedTemp = root.resolve("app-data/config/download_db/categories/categories.json.tmp").toFile()
        try {
            val engine = lifecycle.start()
            val before = categories(engine)
            // The record store stays writable; only the category transactional temp is blocked.
            assertTrue(blockedTemp.mkdir())
            val failure = assertFailsWith<DownloadAddedButCategoryFailed> { add(engine, "committed.zip") }
            assertTrue(failure.downloadId >= 0)
            assertEquals(before, categories(engine))
            assertEquals(failure.downloadId.toString(), engine.snapshot().getValue("rows").jsonArray.single().jsonObject.getValue("id").jsonPrimitive.content)
            assertTrue(blockedTemp.delete())
            lifecycle.stop()
        } finally { blockedTemp.delete(); lifecycle.stop(); root.toFile().deleteRecursively() }
    }

    @Test fun legacyCategoryShapeLongIdsAndIconUrisRoundTripWithoutTruncation() = runBlocking {
        val root = Files.createTempDirectory("abdm-category-legacy-test-")
        val lifecycle = ServiceLifecycle(root)
        try {
            val file = root.resolve("app-data/config/download_db/categories/categories.json").toFile()
            file.parentFile.mkdirs()
            val legacy = listOf(Category(9007199254740993L, "Legacy", "icon:pictureFile", root.resolve("Downloads").toString(), false,
                listOf("zip"), listOf("http://127.0.0.1:*/*"), listOf(9007199254740995L)))
            file.writeText(Json.encodeToString(ListSerializer(Category.serializer()), legacy))
            val engine = lifecycle.start()
            val c = categories(engine).single()
            assertEquals("9007199254740993", c.getValue("id").jsonPrimitive.content)
            assertEquals("9007199254740995", c.getValue("items").jsonArray.single().jsonPrimitive.content)
            engine.command(command("category.update", "categoryId" to c.getValue("id"), *draft("Preserved")))
            lifecycle.stop()
            val stored = Json.decodeFromString(ListSerializer(Category.serializer()), file.readText()).single()
            assertEquals(legacy.single().copy(name = "Preserved", acceptedUrlPatterns = emptyList()), stored)
        } finally { lifecycle.stop(); root.toFile().deleteRecursively() }
    }

    @Test fun categoryIconsUseTheOriginalPickerUrisAndKeepUnofferedOnes() = runBlocking {
        val root = Files.createTempDirectory("abdm-category-icon-test-")
        val lifecycle = ServiceLifecycle(root)
        try {
            val file = root.resolve("app-data/config/download_db/categories/categories.json").toFile()
            file.parentFile.mkdirs()
            // A stored icon this editor does not offer survives edits that omit `icon`.
            file.writeText(Json.encodeToString(ListSerializer(Category.serializer()), listOf(
                Category(5, "Stored", "resource:custom.svg", root.resolve("Downloads").toString(), false, listOf("zip")))))
            val engine = lifecycle.start()
            assertTrue("category.icon" in EngineSession.capabilities)
            val created = engine.command(command("category.create", *draft("Pictures 2", types = listOf("png")), "icon" to JsonPrimitive("icon:fileOpen")))
                .getValue("categoryId").jsonPrimitive.content
            fun iconOf(all: List<JsonObject>, id: String) = all.single { it.getValue("id").jsonPrimitive.content == id }.getValue("icon").jsonPrimitive.content
            assertEquals("icon:fileOpen", iconOf(categories(engine), created))
            val before = file.readText()
            for (bad in listOf(JsonPrimitive("icon:notAnIcon"), JsonPrimitive("file:///tmp/x.svg"), JsonPrimitive(3), JsonNull))
                assertFails { engine.command(command("category.update", "categoryId" to JsonPrimitive(created), *draft("Pictures 2"), "icon" to bad)) }
            assertEquals(before, file.readText())
            engine.command(command("category.update", "categoryId" to JsonPrimitive(created), *draft("Pictures 2"), "icon" to JsonPrimitive("icon:network")))
            engine.command(command("category.update", "categoryId" to JsonPrimitive("5"), *draft("Stored, renamed")))
            lifecycle.stop()
            val stored = Json.decodeFromString(ListSerializer(Category.serializer()), file.readText()).associateBy { it.id }
            assertEquals("icon:network", stored.getValue(created.toLong()).icon)
            assertEquals("resource:custom.svg", stored.getValue(5).icon)
        } finally { lifecycle.stop(); root.toFile().deleteRecursively() }
    }

    @Test fun constructorFilesystemFailurePrecedesMonitorThreadsAndReleasesLease() = runBlocking {
        val root = Files.createTempDirectory("abdm-category-constructor-failure-test-")
        val lifecycle = ServiceLifecycle(root)
        fun ownerThreads() = Thread.getAllStackTraces().keys.filter { it.name == "service-owner" && it.isAlive }.map { it.threadId() }.toSet()
        try {
            val before = ownerThreads()
            val blocked = root.resolve("app-data/config/download_db/categories").toFile()
            blocked.parentFile.mkdirs(); blocked.writeText("not a directory")
            assertFails { lifecycle.start() }
            lifecycle.stop()
            ProfileLease.acquire(root.resolve("app-data/config/app.lock")).use {
                delay(650); assertEquals(before, ownerThreads()); assertEquals("not a directory", blocked.readText())
            }
        } finally { lifecycle.stop(); root.toFile().deleteRecursively() }
    }

    @Test fun corruptAndEscapingCategoriesFailClosedWithoutOverwritingOrLeakingLease() = runBlocking {
        val root = Files.createTempDirectory("abdm-category-corrupt-test-")
        val lifecycle = ServiceLifecycle(root)
        try {
            val file = root.resolve("app-data/config/download_db/categories/categories.json").toFile()
            file.parentFile.mkdirs()
            for (contents in listOf("{corrupt categories", "null", """[{"id":0,"name":"Escape","icon":"icon:pictureFile","path":"/tmp","usePath":true}]""")) {
                file.writeText(contents)
                assertFails { lifecycle.start() }
                lifecycle.stop()
                ProfileLease.acquire(root.resolve("app-data/config/app.lock")).use { delay(650); assertEquals(contents, file.readText()) }
            }
        } finally { lifecycle.stop(); root.toFile().deleteRecursively() }
    }
}
