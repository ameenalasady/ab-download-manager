package com.abdownloadmanager.service

import ir.amirab.downloader.DownloadSettings
import ir.amirab.downloader.db.TransactionalFileSaver
import kotlinx.coroutines.runBlocking
import kotlinx.serialization.json.*
import java.io.File
import java.nio.file.Files
import kotlin.test.*

class SettingsPolicyTest {
    private val repository = generateSequence(File("").absoluteFile) { it.parentFile }.first { File(it, "settings.gradle.kts").isFile }

    /** Drift guard: a new original property must be classified before the service ships. */
    @Test fun everyOriginalDesktopSettingIsClassified() {
        val sources = listOf("shared/app/src/commonMain/kotlin/com/abdownloadmanager/shared/storage/appsettings/sharedAppSettingsDefination.kt",
            "shared/app/src/desktopMain/kotlin/com/abdownloadmanager/shared/storage/appsettings/IAppSettingsModel.desktop.kt")
        val keys = sources.flatMap { Regex("""prop\(\w+::(\w+)\)""").findAll(File(repository, it).readText()).map { m -> m.groupValues[1] }.toList() }.toSet()
        assertEquals(41, keys.size)
        assertEquals(keys, OriginalAppSettings.policies.keys)
    }

    @Test fun activityDependentProfileBootsRetainsModeAndUsesOriginalUnsupportedFallback() {
        val root = Files.createTempDirectory("abdm-bessy-test-")
        try {
            val file = root.resolve("appSettings.json").toFile()
            file.writeText("""{"useSpeedLimit":"EnabledWhenBessy","speedLimit":1024,"apiAuthKey":"secret-key","theme":"layan"}""")
            val settings = EngineSettingsRepository(file, TransactionalFileSaver(Json))
            val engine = DownloadSettings()
            settings.applyTo(engine)
            // Original: shouldUseSpeedLimit = interacting && background; unsupported idle time => not interacting.
            assertEquals(0L, engine.globalSpeedLimit)
            assertEquals("EnabledWhenBessy", settings.snapshot().getValue("useSpeedLimit").jsonPrimitive.content)
            assertFalse(settings.policySnapshot().getValue("speedLimitActive").jsonPrimitive.boolean)
            assertFalse(settings.policySnapshot().getValue("activityAdapter").jsonPrimitive.boolean)
            // Unrelated writes keep the stored mode; it cannot be newly selected without an adapter.
            settings.update(buildJsonObject { put("threadCount", 4) })
            assertEquals("EnabledWhenBessy", Json.parseToJsonElement(file.readText()).jsonObject.getValue("useSpeedLimit").jsonPrimitive.content)
            val before = file.readText()
            assertFails { settings.update(buildJsonObject { put("useSpeedLimit", "EnabledWhenBessy") }) }
            assertEquals(before, file.readText())
            // A real adapter reporting interaction while in background applies the limit.
            val active = object : SystemActivity {
                override fun isUserInteracting() = true
                override fun isFrontendInBackground() = true
                override val available = true
            }
            EngineSettingsRepository(file, TransactionalFileSaver(Json), active).applyTo(engine)
            assertEquals(1024L, engine.globalSpeedLimit)
            settings.update(buildJsonObject { put("useSpeedLimit", "Disabled") })
            settings.applyTo(engine); assertEquals(0L, engine.globalSpeedLimit)
        } finally { root.toFile().deleteRecursively() }
    }

    @Test fun displayPreferencesRoundTripAndRetainedViewNeverLeaksSecretsOrPaths() {
        val root = Files.createTempDirectory("abdm-display-settings-test-")
        try {
            val file = root.resolve("appSettings.json").toFile()
            file.writeText("""{"apiEnabled":true,"apiPort":15151,"apiAuthKey":"secret-key","defaultDownloadFolder":"/home/user/private",""" +
                """"generalNotificationSound":"/home/user/sound.ogg","trackDeletedFilesOnDisk":true,"future":{"a":1}}""")
            val settings = EngineSettingsRepository(file, TransactionalFileSaver(Json))
            assertEquals("BinaryBytes", settings.snapshot().getValue("sizeUnit").jsonPrimitive.content)
            assertTrue(settings.snapshot().getValue("useCategoryByDefault").jsonPrimitive.boolean)
            settings.update(buildJsonObject { put("sizeUnit", "DecimalBits"); put("speedUnit", "BinaryBits"); put("useCategoryByDefault", false) })
            val reloaded = EngineSettingsRepository(file, TransactionalFileSaver(Json)).snapshot()
            assertEquals("DecimalBits", reloaded.getValue("sizeUnit").jsonPrimitive.content)
            assertEquals("BinaryBits", reloaded.getValue("speedUnit").jsonPrimitive.content)
            assertFalse(reloaded.getValue("useCategoryByDefault").jsonPrimitive.boolean)
            val before = file.readText()
            for (invalid in listOf(buildJsonObject { put("sizeUnit", "Nibbles") }, buildJsonObject { put("useCategoryByDefault", "yes") },
                buildJsonObject { put("apiEnabled", false) }, buildJsonObject { put("trackDeletedFilesOnDisk", false) },
                buildJsonObject { put("defaultDownloadFolder", "/tmp") }, buildJsonObject { put("appendExtensionToIncompleteDownloads", true) })) {
                assertFails { settings.update(invalid) }; assertEquals(before, file.readText())
            }
            val stored = Json.parseToJsonElement(file.readText()).jsonObject
            assertEquals("secret-key", stored.getValue("apiAuthKey").jsonPrimitive.content)
            assertEquals(Json.parseToJsonElement("""{"a":1}"""), stored["future"])
            val view = settings.policySnapshot().getValue("retained").jsonArray.associate {
                it.jsonObject.getValue("key").jsonPrimitive.content to it.jsonObject
            }
            assertEquals(OriginalAppSettings.policies.count { it.value == SettingPolicy.Platform || it.value == SettingPolicy.RuntimeOverride }, view.size)
            // Browser API keys are executed by the browser adapter, so they are not "retained only".
            assertTrue(listOf("apiEnabled", "apiPort", "apiAuthEnabled", "apiAuthKey").all {
                OriginalAppSettings.policies[it] == SettingPolicy.Browser && it !in view })
            assertEquals(BrowserSettings(true, 15151, null), settings.browser())
            assertEquals(buildJsonObject { put("enabled", true); put("port", 15151); put("auth", false) }, settings.browserSnapshot())
            assertTrue(view.getValue("trackDeletedFilesOnDisk").getValue("value").jsonPrimitive.boolean)
            // Custom sound files are now executed (notification sound-file hint), so they are
            // integration settings shown for editing, not retained-only; still never in this view.
            assertEquals(SettingPolicy.Integration, OriginalAppSettings.policies["generalNotificationSound"])
            assertFalse("generalNotificationSound" in view)
            assertEquals("/home/user/sound.ogg", settings.integration().getValue("generalNotificationSound").jsonPrimitive.content)
            // Honored within the runtime roots (written through the runtime check), so not "retained only".
            assertEquals(SettingPolicy.Destination, OriginalAppSettings.policies["defaultDownloadFolder"])
            assertFalse("defaultDownloadFolder" in view)
            assertFalse(view.getValue("userAgent").getValue("stored").jsonPrimitive.boolean)
            assertFalse(settings.policySnapshot().toString().contains("secret-key"))
            assertFalse(settings.browserSnapshot().toString().contains("secret-key"))
            assertFalse(settings.policySnapshot().toString().contains("/home/user"))
        } finally { root.toFile().deleteRecursively() }
    }

    @Test fun browserApiSettingsFollowOriginalDefaultsAndFieldFallback() {
        val root = Files.createTempDirectory("abdm-browser-settings-test-")
        try {
            val file = root.resolve("appSettings.json").toFile()
            file.writeText("""{"apiAuthEnabled":true,"apiAuthKey":"  "}""")
            // Original AppRepository: auth needs a non-blank key, otherwise the API stays open.
            assertEquals(BrowserSettings(true, 15151, null), EngineSettingsRepository(file, TransactionalFileSaver(Json)).browser())
            file.writeText("""{"apiAuthEnabled":true,"apiAuthKey":"k1","apiPort":15200,"apiEnabled":false}""")
            val settings = EngineSettingsRepository(file, TransactionalFileSaver(Json))
            assertEquals(BrowserSettings(false, 15200, "k1"), settings.browser())
            val before = file.readText()
            for (invalid in listOf(buildJsonObject { put("apiPort", 0) }, buildJsonObject { put("apiPort", "15151") },
                buildJsonObject { put("apiAuthKey", "has space") }, buildJsonObject { put("threadCount", 2) },
                buildJsonObject { put("apiAuthKey", "") })) {
                assertFails { settings.updateBrowser(invalid) }; assertEquals(before, file.readText())
            }
            settings.updateBrowser(buildJsonObject { put("apiEnabled", true); put("apiPort", 15300) })
            assertEquals(BrowserSettings(true, 15300, "k1"), EngineSettingsRepository(file, TransactionalFileSaver(Json)).browser())
            for (corrupt in listOf("""{"apiPort":70000}""", """{"apiEnabled":"yes"}""", """{"apiAuthKey":5}""")) {
                file.writeText(corrupt)
                val fallback = EngineSettingsRepository(file, TransactionalFileSaver(Json))
                assertEquals(BrowserSettings(true, 15151, null), fallback.browser())
                assertEquals(1, fallback.corruptKeys().size)
                assertEquals(corrupt, file.readText())
            }
        } finally { root.toFile().deleteRecursively() }
    }

    @Test fun corruptFieldsFallbackIndependentlyAndIntegrationWritesAreAtomic() {
        val root = Files.createTempDirectory("abdm-settings-fallback-")
        try {
            val file = root.resolve("settings.json").toFile()
            val raw = """{"threadCount":0,"maxConcurrentDownloads":5,"speedLimit":[],"useSpeedLimit":"nonsense","sizeUnit":42,"autoStartOnBoot":false,"useSystemTray":"yes","notificationSound":{},"apiPort":[],"appendExtensionToIncompleteDownloads":"true","future":{"secret":"retained"}}"""
            file.writeText(raw)
            val r = EngineSettingsRepository(file, TransactionalFileSaver(Json))
            assertEquals(DownloadSettings.DEFAULT_THREAD_COUNT, r.snapshot().getValue("threadCount").jsonPrimitive.int)
            assertEquals(5, r.snapshot().getValue("maxConcurrentDownloads").jsonPrimitive.int)
            assertEquals("262144", r.snapshot().getValue("speedLimit").jsonPrimitive.content)
            assertEquals("BinaryBytes", r.snapshot().getValue("sizeUnit").jsonPrimitive.content)
            assertFalse(r.integration().getValue("autoStartOnBoot").jsonPrimitive.boolean)
            assertTrue(r.integration().getValue("useSystemTray").jsonPrimitive.boolean)
            assertEquals(setOf("threadCount", "speedLimit", "useSpeedLimit", "sizeUnit", "useSystemTray", "notificationSound", "apiPort", "appendExtensionToIncompleteDownloads"), r.corruptKeys().toSet())
            assertEquals(raw, file.readText())
            for (bad in listOf(buildJsonObject { put("useSystemTray", false); put("notificationSound", "no") },
                buildJsonObject { put("autoStartOnBoot", false); put("apiEnabled", false) })) {
                assertFails { r.updateIntegration(bad) }; assertEquals(raw, file.readText())
            }
            r.updateIntegration(buildJsonObject { put("notificationSound", false) })
            r.update(buildJsonObject { put("maxConcurrentDownloads", 2) })
            val stored = Json.parseToJsonElement(file.readText()).jsonObject
            assertEquals(JsonPrimitive(0), stored["threadCount"])
            assertEquals(JsonArray(emptyList()), stored["speedLimit"])
            assertEquals("yes", stored.getValue("useSystemTray").jsonPrimitive.content)
            assertEquals(Json.parseToJsonElement("""{"secret":"retained"}"""), stored["future"])
            assertFalse(EngineSettingsRepository(file, TransactionalFileSaver(Json)).integration().getValue("notificationSound").jsonPrimitive.boolean)
            for (key in listOf("autoStartOnBoot", "notificationSound", "useSystemTray")) assertEquals(SettingPolicy.Integration, OriginalAppSettings.policies[key])
            for (broken in listOf("{bad", "[]", "null")) {
                file.writeText(broken); assertFails { EngineSettingsRepository(file, TransactionalFileSaver(Json)) }
                assertEquals(broken, file.readText())
            }
            file.writeText("{}"); File(file.path + ".tmp").writeText("unfinished")
            assertFails { EngineSettingsRepository(file, TransactionalFileSaver(Json)) }
            assertEquals("unfinished", File(file.path + ".tmp").readText())
        } finally { root.toFile().deleteRecursively() }
    }

    @Test fun serviceSnapshotAdvertisesPolicyAndNeverExecutesRetainedIntegrations() = runBlocking {
        val root = Files.createTempDirectory("abdm-policy-session-test-")
        val lifecycle = ServiceLifecycle(root)
        try {
            val file = root.resolve("app-data/config/appSettings.json").toFile()
            file.parentFile.mkdirs()
            // Original destructive auto-removal enabled and a record whose payload is missing.
            file.writeText("""{"trackDeletedFilesOnDisk":true,"apiEnabled":true,"autoStartOnBoot":true,"useSpeedLimit":"EnabledWhenBessy"}""")
            val engine = lifecycle.start()
            assertTrue("settings.policy" in EngineSession.capabilities && "settings.display" in EngineSession.capabilities)
            val snapshot = engine.snapshot()
            val policy = snapshot.getValue("settingsPolicy").jsonObject
            assertFalse(policy.getValue("speedLimitActive").jsonPrimitive.boolean)
            assertTrue(policy.getValue("retained").jsonArray.any {
                it.jsonObject.getValue("key").jsonPrimitive.content == "trackDeletedFilesOnDisk" && it.jsonObject.getValue("value").jsonPrimitive.boolean
            })
            engine.command(buildJsonObject {
                put("op", "settings.update"); put("settings", buildJsonObject { put("sizeUnit", "DecimalBytes") })
            })
            assertEquals("DecimalBytes", engine.snapshot().getValue("settings").jsonObject.getValue("sizeUnit").jsonPrimitive.content)
            lifecycle.stop()
            val stored = Json.parseToJsonElement(file.readText()).jsonObject
            assertTrue(stored.getValue("trackDeletedFilesOnDisk").jsonPrimitive.boolean)
            assertEquals("EnabledWhenBessy", stored.getValue("useSpeedLimit").jsonPrimitive.content)
            // No integration endpoint/autostart artifact is created by retaining these values.
            assertFalse(root.toFile().walkTopDown().any { it.name.endsWith(".desktop") || it.name == "autostart" })
        } finally { lifecycle.stop(); root.toFile().deleteRecursively() }
    }
}
