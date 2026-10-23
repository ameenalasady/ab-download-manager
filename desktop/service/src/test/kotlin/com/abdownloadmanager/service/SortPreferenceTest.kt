package com.abdownloadmanager.service

import kotlinx.coroutines.runBlocking
import kotlinx.serialization.json.*
import java.nio.file.Files
import kotlin.test.*

class SortPreferenceTest {
    @Test fun sortingPersistsAndRejectsInvalidPatchesAtomically() = runBlocking {
        val root = Files.createTempDirectory("abdm-sort-")
        var lifecycle = ServiceLifecycle(root)
        try {
            var engine = lifecycle.start()
            fun patch(column: String, descending: JsonPrimitive) = buildJsonObject {
                put("op", "settings.update"); put("settings", buildJsonObject {
                    put("qtSortColumn", column); put("qtSortDescending", descending)
                })
            }
            assertTrue("settings.sort" in EngineSession.capabilities)
            engine.command(patch("size", JsonPrimitive(false)))
            assertFails { engine.command(patch("not-a-column", JsonPrimitive(true))) }
            assertFails { engine.command(patch("name", JsonPrimitive("false"))) }
            lifecycle.stop(); lifecycle = ServiceLifecycle(root); engine = lifecycle.start()
            val settings = engine.snapshot().getValue("settings").jsonObject
            assertEquals("size", settings.getValue("qtSortColumn").jsonPrimitive.content)
            assertFalse(settings.getValue("qtSortDescending").jsonPrimitive.boolean)
        } finally { lifecycle.stop(); root.toFile().deleteRecursively() }
    }
}
