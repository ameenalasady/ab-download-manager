package com.abdownloadmanager.service

import com.abdownloadmanager.shared.storage.*
import ir.amirab.downloader.db.TransactionalFileSaver
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.collect
import kotlinx.serialization.json.*
import java.nio.file.Files
import kotlin.test.*

class ExtraSettingsAdapterTest {
    private fun command(op: String, vararg args: Pair<String, JsonElement>) = JsonObject(mapOf("op" to JsonPrimitive(op), *args))
    private fun stored(id: Long) = StoredExtraSettings(Json.parseToJsonElement("""{"id":$id,"powerActionTypeOnFinish":"Shutdown","powerActionUseForceOnFinish":true,"future":{"keep":[1,2]}}""").jsonObject)

    @Test fun realOriginalStoresRetainNumericLongIdsPlatformFieldsAndUnknownFields() = runBlocking {
        val root = Files.createTempDirectory("abdm-extra-adapter-")
        try {
            ProfileLease.acquire(root.resolve("app-data/config/app.lock")).use {
                val folder = root.resolve("extras").toFile().apply { mkdirs() }
                val saver = TransactionalFileSaver(Json)
                val downloads = ExtraDownloadSettingsStorage(folder, saver, StoredExtraSettings, strictReads = true)
                val value = stored(9007199254740993L)
                downloads.setExtraDownloadItemSettings(value)
                downloads.validateStored()
                val queues = ExtraQueueSettingsStorage(folder, saver, StoredExtraSettings, strictReads = true)
                assertEquals(value.document, queues.getExtraQueueSettings(value.id).document)
                assertFalse(Json.parseToJsonElement(folder.resolve("${value.id}.json").readText()).jsonObject.getValue("id").jsonPrimitive.isString)
                queues.deleteExtraQueueSettings(value.id)
                assertFalse(folder.resolve("${value.id}.json").exists())
                assertEquals(JsonNull, downloads.getExtraDownloadItemSettings(value.id).document["powerActionTypeOnFinish"])
            }
        } finally { root.toFile().deleteRecursively() }
    }

    @Test fun failedWritesDoNotEmitUncommittedSettingsAndDeletionSharesTheWriterLock() = runBlocking {
        val root = Files.createTempDirectory("abdm-extra-events-")
        try {
            val folder = root.toFile()
            val store = ExtraDownloadSettingsStorage(folder, TransactionalFileSaver(Json), StoredExtraSettings, strictReads = true)
            val events = mutableListOf<StoredExtraSettings>()
            val subscription = launch(start = CoroutineStart.UNDISPATCHED) {
                store.getExternalDownloadItemSettingsAsFlow(1, false).collect { events.add(it) }
            }
            val blocked = folder.resolve("1.json.tmp").apply { mkdir() }
            assertFails { store.setExtraDownloadItemSettings(stored(1)) }
            yield(); assertTrue(events.isEmpty())
            assertTrue(blocked.delete())
            store.setExtraDownloadItemSettings(stored(1)); yield()
            assertEquals(stored(1).document, events.single().document)
            store.deleteExtraDownloadItemSettings(1); yield()
            assertEquals(2, events.size)
            assertEquals(JsonNull, events.last().document["powerActionTypeOnFinish"])
            subscription.cancelAndJoin()
        } finally { root.toFile().deleteRecursively() }
    }

    @Test fun concurrentReadersNeverTreatOurOwnTransactionAsCorruptionAndFlowsKeepTheLatestCommit() = runBlocking {
        val root = Files.createTempDirectory("abdm-extra-concurrency-")
        try {
            val store = ExtraDownloadSettingsStorage(root.toFile(), TransactionalFileSaver(Json), StoredExtraSettings, strictReads = true)
            var observed: StoredExtraSettings? = null
            val collector = launch(start = CoroutineStart.UNDISPATCHED) {
                store.getExternalDownloadItemSettingsAsFlow(1, true).collect { observed = it }
            }
            coroutineScope {
                repeat(8) { writer -> launch(Dispatchers.Default) {
                    repeat(20) { iteration ->
                        val value = StoredExtraSettings(JsonObject(stored(1).document + ("sequence" to JsonPrimitive(writer * 20 + iteration))))
                        store.setExtraDownloadItemSettings(value)
                        assertEquals(1L, store.getExtraDownloadItemSettings(1).id)
                    }
                } }
            }
            val final = StoredExtraSettings(JsonObject(stored(1).document + ("sequence" to JsonPrimitive(1001))))
            store.setExtraDownloadItemSettings(final)
            withTimeout(3000) { while (observed?.document != final.document) delay(1) }
            assertEquals(final.document, store.getExtraDownloadItemSettings(1).document)
            collector.cancelAndJoin()
        } finally { root.toFile().deleteRecursively() }
    }

    @Test fun facadeCleanupDeletesRealExtraSettingsButNeverPayloadsOrExecutesPowerActions() = runBlocking {
        val root = Files.createTempDirectory("abdm-extra-facade-")
        val owner = ServiceLifecycle(root)
        try {
            var engine = owner.start()
            val id = engine.command(command("add", "url" to JsonPrimitive("http://127.0.0.1:12345/fixture"), "name" to JsonPrimitive("keep.bin"))).getValue("id").jsonPrimitive.content
            val queueId = engine.command(command("queue.create", "name" to JsonPrimitive("Disposable"))).getValue("queueId").jsonPrimitive.content
            owner.stop()
            val extras = root.resolve("app-data/config/download_db")
            val downloadFile = extras.resolve("extra_download_settings/$id.json").toFile().apply { writeText(stored(id.toLong()).document.toString()) }
            val queueFile = extras.resolve("extra_queue_settings/$queueId.json").toFile().apply { writeText(stored(queueId.toLong()).document.toString()) }
            val payload = root.resolve("Downloads/keep.bin").toFile().apply { writeText("payload stays byte-for-byte") }
            engine = owner.start()
            assertFalse(engine.snapshot().toString().contains("powerActionTypeOnFinish"))
            engine.command(command("queue.delete", "queueId" to JsonPrimitive(queueId)))
            engine.command(command("download.forget", "id" to JsonPrimitive(id), "keepFiles" to JsonPrimitive(true)))
            assertFalse(downloadFile.exists()); assertFalse(queueFile.exists())
            assertEquals("payload stays byte-for-byte", payload.readText())
            owner.stop()
            ProfileLease.acquire(root.resolve("app-data/config/app.lock")).use {
                val before = payload.lastModified(); delay(600)
                assertEquals(before, payload.lastModified())
            }
            engine = owner.start(); assertTrue(engine.snapshot().getValue("rows").jsonArray.isEmpty())
        } finally { owner.stop(); root.toFile().deleteRecursively() }
    }

    @Test fun corruptMismatchedAndUnfinishedExtraDocumentsRejectWithoutRewriting() = runBlocking {
        for ((name, content) in listOf("3.json" to "{broken", "3.json" to "{\"id\":4}", "3.json.tmp" to "{\"id\":3}", "bad.json" to "{}")) {
            val root = Files.createTempDirectory("abdm-extra-corrupt-")
            val owner = ServiceLifecycle(root)
            try {
                val file = root.resolve("app-data/config/download_db/extra_download_settings/$name").toFile()
                file.parentFile.mkdirs(); file.writeText(content)
                assertFails { owner.start() }; owner.stop()
                assertEquals(content, file.readText())
                ProfileLease.acquire(root.resolve("app-data/config/app.lock")).close()
            } finally { owner.stop(); root.toFile().deleteRecursively() }
        }
    }

    @Test fun originalQueueStoreMustReportAnUnremovablePrimaryRatherThanPretendSuccess() = runBlocking {
        val root = Files.createTempDirectory("abdm-queue-delete-failure-")
        try {
            val blocked = root.resolve("11.json").toFile().apply { mkdir() }
            val evidence = blocked.resolve("evidence").apply { writeText("preserved") }
            val store = ir.amirab.downloader.db.DownloadQueueFileStorageDatabase(TransactionalFileSaver(Json), root.toFile())
            assertFails { store.deleteQueue(11) }
            assertEquals("preserved", evidence.readText())
        } finally { root.toFile().deleteRecursively() }
    }

    @Test fun extraCleanupFailureReportsPartialQueueDeletionAndRetainsEvidence() = runBlocking<Unit> {
        val root = Files.createTempDirectory("abdm-extra-partial-")
        val owner = ServiceLifecycle(root)
        try {
            val engine = owner.start()
            val id = engine.command(command("queue.create", "name" to JsonPrimitive("Partial"))).getValue("queueId").jsonPrimitive.content
            val blocked = root.resolve("app-data/config/download_db/extra_queue_settings/$id.json").toFile()
            blocked.mkdir(); blocked.resolve("evidence").writeText("keep")
            val failure = assertFailsWith<QueueDeletePartiallyApplied> { engine.command(command("queue.delete", "queueId" to JsonPrimitive(id))) }
            assertEquals(id.toLong(), failure.queueId)
            assertTrue(engine.snapshot().getValue("queues").jsonArray.none { it.jsonObject["id"] == JsonPrimitive(id) })
            assertEquals("keep", blocked.resolve("evidence").readText())
            blocked.deleteRecursively() // Only this injected disposable obstruction.
        } finally { owner.stop(); root.toFile().deleteRecursively() }
    }
}
