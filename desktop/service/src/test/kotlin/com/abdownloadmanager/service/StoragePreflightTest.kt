package com.abdownloadmanager.service

import kotlinx.coroutines.runBlocking
import kotlinx.serialization.json.*
import java.io.File
import java.nio.file.Files
import kotlin.test.*

class StoragePreflightTest {
    private suspend fun seed(owner: ServiceLifecycle) {
        val engine = owner.start()
        engine.command(buildJsonObject { put("op", "add"); put("url", "http://127.0.0.1:12345/fixture"); put("name", "saved.bin") })
        owner.stop()
    }
    private fun evidence(root: File) = root.walkTopDown().filter { it.isFile }.associate {
        it.relativeTo(root).path to (it.readBytes().toList() to it.lastModified())
    }

    @Test fun corruptStorageAndInterruptedTransactionsFailClosedBeforeAnyWriter() = runBlocking {
        val cases: List<Pair<String, (File) -> Unit>> = listOf(
            "corrupt record" to { db -> db.resolve("downloadlist/0.json").writeText("{corrupt") },
            "mismatched record" to { db ->
                val file = db.resolve("downloadlist/0.json")
                val doc = Json.parseToJsonElement(file.readText()).jsonObject
                file.writeText(JsonObject(doc + ("id" to JsonPrimitive(4))).toString())
            },
            "noncanonical filename" to { db -> check(db.resolve("downloadlist/0.json").renameTo(db.resolve("downloadlist/00.json"))) },
            "missing high water" to { db -> check(db.resolve("downloadlist/last_id.txt").delete()) },
            "regressed high water" to { db -> db.resolve("downloadlist/last_id.txt").writeText("-1") },
            "corrupt high water" to { db -> db.resolve("downloadlist/last_id.txt").writeText("not a number") },
            "exhausted high water" to { db -> db.resolve("downloadlist/last_id.txt").writeText(Long.MAX_VALUE.toString()) },
            "corrupt parts" to { db -> db.resolve("parts/0.json").writeText("{corrupt") },
            "negative progress" to { db -> db.resolve("parts/0.json").writeText("""{"type":"ranges","list":[{"from":0,"to":99,"current":-1}]}""") },
            "overlapping parts" to { db -> db.resolve("parts/0.json").writeText("""{"type":"ranges","list":[{"from":0,"to":99,"current":0},{"from":50,"to":199,"current":50}]}""") },
            "record transaction" to { db -> db.resolve("downloadlist/0.json.tmp").writeText("unfinished") },
            "part transaction" to { db -> db.resolve("parts/0.json.tmp").writeText("unfinished") },
            "queue transaction" to { db -> db.resolve("queues/0.json.tmp").writeText("unfinished") },
            "category transaction" to { db -> db.resolve("categories/categories.json.tmp").writeText("unfinished") },
            "settings transaction" to { db -> db.parentFile.resolve("appSettings.json.tmp").writeText("unfinished") },
        )
        for ((name, corrupt) in cases) {
            val root = Files.createTempDirectory("abdm-storage-matrix-")
            val owner = ServiceLifecycle(root)
            try {
                seed(owner)
                corrupt(root.resolve("app-data/config/download_db").toFile())
                val before = evidence(root.toFile())
                assertFails(name) { owner.start() }; owner.stop()
                assertEquals(before, evidence(root.toFile()), name)
                ProfileLease.acquire(root.resolve("app-data/config/app.lock")).close()
            } finally { owner.stop(); root.toFile().deleteRecursively() }
        }
    }

    @Test fun committedHistoryAndOrphanPartTransactionSurviveRestartWithoutResurrection() = runBlocking {
        val root = Files.createTempDirectory("abdm-storage-restart-")
        val owner = ServiceLifecycle(root)
        try {
            seed(owner)
            val orphan = root.resolve("app-data/config/download_db/parts/77.json.tmp").toFile().apply { writeText("retained partial-cleanup evidence") }
            val engine = owner.start()
            val rows = engine.snapshot().getValue("rows").jsonArray
            assertEquals(1, rows.size); assertEquals("0", rows.single().jsonObject.getValue("id").jsonPrimitive.content)
            assertEquals("retained partial-cleanup evidence", orphan.readText())
            owner.stop()
            assertEquals("retained partial-cleanup evidence", orphan.readText())
        } finally { owner.stop(); root.toFile().deleteRecursively() }
    }
}
