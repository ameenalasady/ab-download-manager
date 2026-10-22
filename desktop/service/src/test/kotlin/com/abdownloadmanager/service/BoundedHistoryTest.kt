package com.abdownloadmanager.service

import kotlinx.serialization.json.*
import kotlin.test.*

class BoundedHistoryTest {
    private fun rows(count: Int) = (0 until count).map { index -> buildJsonObject {
        put("id", (9007199254740993L + index).toString()); put("name", "fixture-$index.bin")
        put("size", "1048576"); put("state", "Paused"); put("dateAdded", index.toString())
    } }
    @Test fun stableWindowsBoundedDeltasAndNoCrossEpochCursorReuse() {
        val pager = BoundedHistory(); val rows = rows(501)
        val first = pager.query(rows, buildJsonObject { put("limit", 200) })
        assertEquals(200, first.getValue("rows").jsonArray.size)
        val unchanged = pager.query(rows, buildJsonObject {
            put("limit", 200); put("cursor", first.getValue("cursor")); put("known", first.getValue("hashes"))
        })
        assertTrue(unchanged.getValue("rows").jsonArray.isEmpty())
        val changed = rows.toMutableList()
        changed[0] = JsonObject(changed[0] + ("state" to JsonPrimitive("Completed")))
        val delta = pager.query(changed, buildJsonObject {
            put("limit", 200); put("cursor", first.getValue("cursor")); put("known", first.getValue("hashes"))
        })
        assertEquals(1, delta.getValue("rows").jsonArray.size)
        val second = pager.query(changed, buildJsonObject {
            put("limit", 200); put("cursor", first.getValue("nextCursor")); put("known", first.getValue("hashes"))
        })
        assertEquals(200, second.getValue("removed").jsonArray.size)
        assertEquals(rows[200].getValue("id"), second.getValue("order").jsonArray[0])
        assertFails { pager.query(changed.drop(1), buildJsonObject { put("cursor", first.getValue("cursor")) }) }
        assertFails { BoundedHistory().query(rows, buildJsonObject { put("cursor", first.getValue("cursor")) }) }
        for (limit in listOf(-1, 0, 201, Int.MAX_VALUE)) assertFails { pager.query(rows, buildJsonObject { put("limit", limit) }) }
        assertFails { pager.query(rows, buildJsonObject { put("cursor", "unbounded-offset:9999999999999999999999") }) }
    }
    @Test fun generatedSummaryKernelBenchmarksNotEngineOrProcessTreeAcceptance() {
        for (count in listOf(1000, 10000, 50000)) {
            val rows = rows(count); val pager = BoundedHistory()
            val start = System.nanoTime()
            val page = pager.query(rows, buildJsonObject { put("limit", 200) })
            val firstNanos = System.nanoTime() - start
            val deltaStart = System.nanoTime()
            val delta = pager.query(rows, buildJsonObject {
                put("limit", 200); put("cursor", page.getValue("cursor")); put("known", page.getValue("hashes"))
            })
            val deltaNanos = System.nanoTime() - deltaStart
            assertTrue(delta.getValue("rows").jsonArray.isEmpty())
            assertTrue(page.toString().toByteArray().size < 256 * 1024)
            assertEquals("9007199254740993", page.getValue("order").jsonArray[0].jsonPrimitive.content)
            println("HISTORY_KERNEL count=$count pageNanos=$firstNanos unchangedDeltaNanos=$deltaNanos pageBytes=${page.toString().toByteArray().size} scope=summary-kernel-only")
        }
    }
}
