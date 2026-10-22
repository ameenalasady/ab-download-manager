package com.abdownloadmanager.service

import ir.amirab.downloader.*
import ir.amirab.downloader.connection.HttpDownloaderClient
import ir.amirab.downloader.db.*
import ir.amirab.downloader.downloaditem.*
import ir.amirab.downloader.downloaditem.http.*
import ir.amirab.downloader.queue.ManualDownloadQueue
import ir.amirab.downloader.queue.DownloadQueue
import ir.amirab.downloader.utils.*
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.serialization.json.*
import java.nio.file.Files
import java.util.concurrent.Executors
import kotlin.test.*

class DownloadForgetTest {
    private fun command(op: String, vararg args: Pair<String, JsonElement>) = JsonObject(mapOf("op" to JsonPrimitive(op), *args))
    private suspend fun add(engine: EngineSession, name: String) = engine.command(command("add",
        "url" to JsonPrimitive("http://127.0.0.1:12345/fixture"), "name" to JsonPrimitive(name))).getValue("id").jsonPrimitive.content
    private suspend fun forget(engine: EngineSession, id: String) = engine.command(command("download.forget",
        "id" to JsonPrimitive(id), "keepFiles" to JsonPrimitive(true)))

    @Test fun metadataOnlyRemovalKeepsEveryPayloadAndDrainsLateWriters() = runBlocking {
        val root = Files.createTempDirectory("abdm-forget-core-")
        val memory = MemoryDownloadListDB()
        var failRemoval = false
        val list = object : IDownloadListDb by memory {
            override suspend fun remove(item: IDownloadItem) {
                check(!failRemoval) { "Injected record deletion failure" }
                memory.remove(item)
            }
        }
        val parts = PartListFileStorage(root.resolve("parts").toFile().apply { mkdirs() }, TransactionalFileSaver(Json))
        val registry = DownloaderRegistry().apply { add(HttpDownloader(lazy { object : HttpDownloaderClient() {
            override suspend fun actualHead(credentials: IHttpDownloadCredentials, start: Long?, end: Long?): ir.amirab.downloader.connection.response.HttpResponseInfo = error("No network permitted")
            override suspend fun actualConnect(credentials: IHttpBasedDownloadCredentials, start: Long?, end: Long?): ir.amirab.downloader.connection.Connection<ir.amirab.downloader.connection.response.HttpResponseInfo> = error("No network permitted")
        } })) }
        val manager = DownloadManager(list, parts, DownloadSettings(), EmptyFileCreator(object : IDiskStat {
            override fun getRemainingSpace(path: java.io.File) = path.usableSpace
        }, { false }), registry, root.toFile())
        try {
            val item = HttpDownloadItem(link = "http://127.0.0.1:12345/fixture", id = 9007199254740993L,
                folder = root.toString(), name = "kept.bin", status = DownloadStatus.Paused)
            list.add(item); manager.boot()
            val payload = root.resolve("kept.bin").toFile().apply { writeText("incomplete output") }
            val incomplete = root.resolve("kept.bin.part").toFile().apply { writeText("incomplete temporary") }
            val job = manager.downloadJobs.single()
            val started = CompletableDeferred<Unit>(); var finalizerDrained = false
            job.scope.launch {
                try { started.complete(Unit); awaitCancellation() }
                finally { withContext(NonCancellable) { delay(100); list.update(item); finalizerDrained = true } }
            }
            started.await()
            failRemoval = true
            assertFailsWith<IllegalStateException> { manager.forgetDownloadKeepingFiles(item.id) }
            assertTrue(finalizerDrained); assertTrue(job.scope.coroutineContext.job.isCompleted)
            assertNotNull(list.getById(item.id))
            assertNotSame(job, manager.downloadJobs.single()) // Recovered, usable scope, not the drained one.
            assertTrue(manager.downloadJobs.single().scope.coroutineContext.job.isActive)
            failRemoval = false
            manager.forgetDownloadKeepingFiles(item.id)
            assertTrue(finalizerDrained); assertTrue(job.scope.coroutineContext.job.isCompleted)
            assertTrue(manager.downloadJobs.isEmpty()); assertNull(list.getById(item.id))
            delay(150); assertNull(list.getById(item.id))
            assertEquals("incomplete output", payload.readText()); assertEquals("incomplete temporary", incomplete.readText())
            assertFalse(parts.getFileForId(item.id).exists())
            // Completed history must not construct/boot a destination just to forget it.
            registry.getAll().forEach(registry::remove)
            list.add(item.copy(status = DownloadStatus.Completed))
            manager.forgetDownloadKeepingFiles(item.id)
            assertNull(list.getById(item.id)); assertEquals("incomplete output", payload.readText())
        } finally {
            manager.downloadJobs.forEach { it.scope.coroutineContext.job.cancelAndJoin() }
            manager.scope.coroutineContext.job.cancelAndJoin(); root.toFile().deleteRecursively()
        }
    }

    @Test fun serviceRemovalPersistsMembershipAndNeverResurrectsRemovedRecords() = runBlocking {
        val root = Files.createTempDirectory("abdm-forget-service-")
        val lifecycle = ServiceLifecycle(root)
        try {
            var engine = lifecycle.start()
            val id = add(engine, "kept.zip")
            engine.command(command("queue.add", "queueId" to JsonPrimitive("0"), "id" to JsonPrimitive(id)))
            assertFails { engine.command(command("download.forget", "id" to JsonPrimitive(id))) }
            assertFails { engine.command(command("download.forget", "id" to JsonPrimitive(id), "keepFiles" to JsonPrimitive(false))) }
            val output = root.resolve("Downloads/kept.zip").toFile().apply { writeText("keep me") }
            val temporary = root.resolve("Downloads/kept.zip.abdmtemp").toFile().apply { writeText("keep me too") }
            forget(engine, id)
            assertTrue(engine.snapshot().getValue("rows").jsonArray.isEmpty())
            assertTrue(engine.snapshot().getValue("queues").jsonArray.all { id !in it.jsonObject.getValue("items").jsonArray.map { it.jsonPrimitive.content } })
            assertTrue(engine.snapshot().getValue("categories").jsonArray.all { id !in it.jsonObject.getValue("items").jsonArray.map { it.jsonPrimitive.content } })
            lifecycle.stop()
            val config = root.resolve("app-data/config").toFile()
            fun persisted() = config.walkTopDown().filter { it.isFile }.associate { it.path to (it.readBytes().toList() to it.lastModified()) }
            val settled = persisted()
            ProfileLease.acquire(root.resolve("app-data/config/app.lock")).use { delay(1100); assertEquals(settled, persisted()) }
            engine = lifecycle.start()
            assertTrue(engine.snapshot().getValue("rows").jsonArray.isEmpty())
            assertTrue(add(engine, "new.zip").toLong() > id.toLong()) // Never reuse a forgotten ID.
            assertEquals("keep me", output.readText()); assertEquals("keep me too", temporary.readText())
        } finally { lifecycle.stop(); root.toFile().deleteRecursively() }
    }

    @Test fun persistenceFailuresReportPartialOutcomeWithoutDeletingPayloadOrResurrectingHistory() = runBlocking {
        val root = Files.createTempDirectory("abdm-forget-failure-")
        val lifecycle = ServiceLifecycle(root)
        val blocked = root.resolve("app-data/config/download_db/categories/categories.json.tmp").toFile()
        var partBlock: java.io.File? = null
        try {
            var engine = lifecycle.start()
            val id = add(engine, "kept.zip")
            val output = root.resolve("Downloads/kept.zip").toFile().apply { writeText("unaltered") }
            assertTrue(blocked.mkdir())
            assertEquals(id.toLong(), assertFailsWith<DownloadForgetPartiallyApplied> { forget(engine, id) }.downloadId)
            assertEquals(id, engine.snapshot().getValue("rows").jsonArray.single().jsonObject.getValue("id").jsonPrimitive.content)
            assertTrue(blocked.delete())
            // Fail the final internal part cleanup AFTER primary history deletion.
            partBlock = root.resolve("app-data/config/download_db/parts/$id.json").toFile()
            partBlock.delete(); assertTrue(partBlock.mkdir()); partBlock.resolve("block").writeText("not empty")
            assertFailsWith<DownloadForgetPartiallyApplied> { forget(engine, id) }
            assertTrue(engine.snapshot().getValue("rows").jsonArray.isEmpty())
            partBlock.deleteRecursively(); partBlock = null
            lifecycle.stop(); engine = lifecycle.start()
            assertTrue(engine.snapshot().getValue("rows").jsonArray.isEmpty())
            assertEquals("unaltered", output.readText())
        } finally { blocked.delete(); partBlock?.deleteRecursively(); lifecycle.stop(); root.toFile().deleteRecursively() }
    }

    @Test fun detachedManualAndNamedWorkCannotRunAnAlreadyQueuedStart() = runBlocking {
        val owner = Executors.newSingleThreadExecutor().asCoroutineDispatcher()
        val scope = CoroutineScope(SupervisorJob() + owner)
        val started = mutableListOf<Long>()
        val events = object : DownloadManagerMinimalControl {
            override val listOfJobsEvents = MutableSharedFlow<DownloadManagerEvents>()
            override suspend fun startJob(id: Long, context: DownloadItemContext) { started.add(id) }
            override suspend fun stopJob(id: Long, context: DownloadItemContext) = Unit
            override fun canActivateJob(id: Long) = true
        }
        try {
            withContext(owner) {
                val queue = ManualDownloadQueue(events, scope)
                queue.boot(); queue.resume(1); queue.removeFromQueue(1)
                val model = QueueModel(1, "Test", queueItems = listOf(2))
                val persisted = object : DownloadQueuePersistedDataAccess {
                    var stored = model
                    override suspend fun setModel(queue: QueueModel) { stored = queue }
                    override suspend fun getModel() = stored
                }
                val named = DownloadQueue(model, persisted, events, {}, scope.coroutineContext)
                named.boot(); named.start(); named.removeFromQueue(2)
                // Let already-queued starts run before closing; neither detached ID may start.
                yield(); assertTrue(started.isEmpty()); named.closeAndJoin()
            }
            withContext(owner) { assertTrue(started.isEmpty()) }
        } finally { scope.coroutineContext.job.cancelAndJoin(); owner.close() }
    }
}
