package com.abdownloadmanager.service

import ir.amirab.downloader.DownloadSettings
import com.abdownloadmanager.shared.util.DownloadEnginePolicy
import ir.amirab.downloader.db.TransactionalFileSaver
import kotlinx.serialization.json.*
import java.io.File

/** Adapter for the existing flat appSettings.json, not a parallel fixture preferences file.
 * Every original property is classified by [OriginalAppSettings.policies]. Engine, display,
 * browser and integration properties are writable; everything else is retained losslessly.
 * Writes are synchronous/transactional under the service command gate: no hidden writer scope.
 *
 * Corrupt VALUES follow the original per-field `.catch(default)`: the default is used and the
 * key is reported in [corruptKeys]. Unlike the original, the stored value is left untouched on
 * disk until that field is written. An unparseable document or an unfinished transaction
 * still fails closed (explicit recovery), never silently resets every setting.
 */
class EngineSettingsRepository(
    private val file: File,
    private val saver: TransactionalFileSaver,
    private val activity: SystemActivity = UnsupportedSystemActivity,
) {
    init { require(!saver.getBakFile(file).exists()) { "Unfinished settings transaction; explicit recovery required" } }
    private var document = if (file.exists()) {
        require(file.length() <= 1024 * 1024) { "Settings document too large" }
        val parsed = runCatching { Json.parseToJsonElement(file.readText()) }.getOrNull()
        require(parsed is JsonObject) { "Settings document is not a JSON object; explicit recovery required" }
        parsed
    } else JsonObject(emptyMap())

    // Defaults and bounds match DefaultAppSettings/sharedAppSettingsDefinition (desktop).
    private val defaults = buildJsonObject {
        put("threadCount", DownloadSettings.DEFAULT_THREAD_COUNT)
        put("maxConcurrentDownloads", 3); put("maxDownloadRetryCount", 3)
        put("minPartSize", DownloadSettings.DEFAULT_MIN_PART_SIZE.toString())
        put("dynamicPartCreation", true); put("useServerLastModifiedTime", false)
        put("useSparseFileAllocation", true)
        put("useAverageSpeed", true); put("useSpeedLimit", "Disabled"); put("speedLimit", "262144")
        put("sizeUnit", "BinaryBytes"); put("speedUnit", "BinaryBytes"); put("useCategoryByDefault", true)
        // Qt frontend only (the original asks on every duplicate): what Add does when the link or
        // destination already exists. Never an automatic overwrite; that stays a per-add choice.
        put("qtDuplicateAction", "ask")
        put("qtSortColumn", "dateAdded"); put("qtSortDescending", true)
    }
    private val writable = defaults.keys
    private val browserKeys = setOf("apiEnabled", "apiPort", "apiAuthEnabled", "apiAuthKey")
    private val notificationSoundKeys = listOf("successNotificationSound", "errorNotificationSound", "generalNotificationSound")
    private val integrationDefaults = buildJsonObject {
        put("autoStartOnBoot", true); put("useSystemTray", true); put("notificationSound", true)
        // DefaultAppSettings: both true. The frontend opens these windows; the service never does.
        put("showDownloadProgressDialog", true); put("showDownloadCompletionDialog", true)
        // Original custom sound files: "" is the default sound, otherwise an absolute path.
        for (key in notificationSoundKeys) put(key, "")
    }

    /** Strict validation of one value; throws with a user-facing message. */
    private fun value(key: String, p: JsonPrimitive): JsonPrimitive = when (key) {
        "threadCount" -> { require(!p.isString && p.int in 1..256) { "Thread count must be 1–256" }; JsonPrimitive(p.int) }
        "maxConcurrentDownloads", "maxDownloadRetryCount" -> {
            require(!p.isString && p.int in 0..1024) { "$key must be 0–1024" }; JsonPrimitive(p.int)
        }
        "minPartSize" -> {
            val n = p.content.toLong()
            require(n in DownloadSettings.MIN_ALLOWED_MIN_PART_SIZE..DownloadSettings.MAX_ALLOWED_MIN_PART_SIZE) { "Invalid minimum part size" }
            JsonPrimitive(n.toString()) // IPC long values are strings; disk uses original numeric representation.
        }
        "useSpeedLimit" -> {
            // The original activity-dependent mode is retained and evaluated (see policy()).
            require(p.isString && p.content in setOf("Enabled", "Disabled", "EnabledWhenBessy")) { "Invalid speed limit mode" }
            JsonPrimitive(p.content)
        }
        "sizeUnit", "speedUnit" -> {
            require(p.isString && p.content in OriginalAppSettings.sizeUnits) { "Unsupported $key" }; JsonPrimitive(p.content)
        }
        "speedLimit" -> { val n = p.content.toLong(); require(n > 0) { "Speed limit must be positive" }; JsonPrimitive(n.toString()) }
        "qtSortColumn" -> {
            require(p.isString && p.content in setOf("name", "size", "state", "speed", "remainingTime", "dateAdded")) { "Unsupported sort column" }; JsonPrimitive(p.content)
        }
        "qtDuplicateAction" -> {
            require(p.isString && p.content in setOf("ask", "numbered", "existing")) { "Unsupported duplicate action" }; JsonPrimitive(p.content)
        }
        in notificationSoundKeys -> {
            // Stored values are only checked for shape; existence is checked when written and played.
            require(p.isString && (p.content.isEmpty() || (p.content.length <= 4096 && p.content.none(Char::isISOControl)
                && java.nio.file.Path.of(p.content).isAbsolute))) { "Invalid notification sound" }
            JsonPrimitive(p.content)
        }
        else -> { require(!p.isString && p.booleanOrNull != null) { "$key must be boolean" }; JsonPrimitive(p.boolean) }
    }

    private fun stored(key: String) = document[key]?.takeIf { it !is JsonNull }

    /** Per-field original fallback: an absent or invalid stored value yields the default. */
    private fun effective(key: String, default: JsonPrimitive, check: (JsonPrimitive) -> JsonPrimitive): JsonPrimitive {
        val raw = stored(key) ?: return check(default)
        return runCatching { check(raw as JsonPrimitive) }.getOrElse { check(default) }
    }

    private fun valid(key: String, check: (JsonPrimitive) -> Unit): Boolean {
        val raw = stored(key) ?: return true
        return runCatching { check(raw as JsonPrimitive) }.isSuccess
    }

    /** Keys whose stored value is invalid and therefore replaced by its default. Keys only. */
    fun corruptKeys(): List<String> =
        defaults.keys.filterNot { valid(it) { p -> value(it, p) } } +
        browserKeys.filterNot { valid(it) { p -> browserValue(it, p) } } +
        integrationDefaults.keys.filterNot { valid(it) { p -> value(it, p) } } +
        listOf("appendExtensionToIncompleteDownloads").filterNot { valid(it) { p -> value(it, p) } }

    fun snapshot() = buildJsonObject {
        for ((key, default) in defaults) put(key, effective(key, default.jsonPrimitive) { value(key, it) })
    }

    fun update(patch: JsonObject) = updateValues(patch, null)

    /** Caller validates the destination against RuntimePolicy before this atomic preference write. */
    fun updateWithDefaultFolder(patch: JsonObject, folder: String?) {
        require(folder == null || folder.length in 1..4096) { "Invalid default download folder" }
        updateValues(patch, folder?.let(::JsonPrimitive) ?: JsonNull)
    }

    private fun updateValues(patch: JsonObject, folder: JsonElement?) {
        require((patch.isNotEmpty() || folder != null) && patch.keys.all { it in writable }) { "Unsupported setting" }
        // Writing it would silently replace a manual limit with an activity monitor this
        // service may not have. A stored value is kept until the user picks another mode.
        require(activity.selectable || patch["useSpeedLimit"]?.jsonPrimitive?.contentOrNull != "EnabledWhenBessy") {
            "Activity-dependent speed limit requires a system activity adapter"
        }
        // Validate the whole patch before writing anything.
        val values = patch.mapValues { (key, element) ->
            require(element is JsonPrimitive) { "Invalid $key" }
            value(key, element)
        }
        val changes: Map<String, JsonElement> = values.mapValues { (key, v) ->
            if (key == "minPartSize" || key == "speedLimit") JsonPrimitive(v.content.toLong()) else v
        }
        write(if (folder == null) changes else changes + ("defaultDownloadFolder" to folder))
    }

    private fun write(changes: Map<String, JsonElement>) {
        val merged = JsonObject(document + changes)
        saver.writeObject(file, merged)
        document = merged // Publish only after persistence succeeds.
    }

    fun applyTo(settings: DownloadSettings) = policy().applyTo(settings)

    /** Stored original defaultDownloadFolder, unvalidated: the caller checks it against the runtime. */
    fun defaultFolder(): String? = (stored("defaultDownloadFolder") as? JsonPrimitive)?.takeIf { it.isString && it.content.isNotEmpty() }?.content

    private fun browserValue(key: String, p: JsonPrimitive): JsonPrimitive = when (key) {
        "apiPort" -> { require(!p.isString && p.int in 1..65535) { "apiPort must be 1–65535" }; JsonPrimitive(p.int) }
        "apiAuthKey" -> { require(p.isString) { "apiAuthKey must be text" }; p }
        else -> { require(!p.isString && p.booleanOrNull != null) { "$key must be boolean" }; JsonPrimitive(p.boolean) }
    }

    /** Original browser API settings: defaults and key handling of DefaultAppSettings and
     * AppRepository (auth applies only with a non-blank key). Invalid values use defaults.
     */
    fun browser(): BrowserSettings {
        val enabled = effective("apiEnabled", JsonPrimitive(true)) { browserValue("apiEnabled", it) }.boolean
        val port = effective("apiPort", JsonPrimitive(15151)) { browserValue("apiPort", it) }.int
        val auth = effective("apiAuthEnabled", JsonPrimitive(false)) { browserValue("apiAuthEnabled", it) }.boolean
        val key = effective("apiAuthKey", JsonPrimitive("")) { browserValue("apiAuthKey", it) }.content
        return BrowserSettings(enabled, port, key.takeIf { auth && it.isNotBlank() })
    }

    /** Writes only the original browser API keys. The key is write-only: never in snapshots. */
    fun updateBrowser(patch: JsonObject) {
        require(patch.isNotEmpty() && patch.keys.all { it in browserKeys }) { "Unsupported browser setting" }
        val values = patch.mapValues { (key, element) ->
            require(element is JsonPrimitive) { "Invalid $key" }
            browserValue(key, element)
        }
        values["apiAuthKey"]?.let {
            require(it.content.length in 1..256 && it.content.none { c -> c.isWhitespace() || c.isISOControl() }) { "API key must be 1–256 visible characters" }
        }
        val previous = document
        document = JsonObject(document + values)
        val checked = try { browser() } finally { document = previous }
        require(!(values["apiAuthEnabled"]?.boolean == true && checked.apiKey == null)) { "Set an API key before enabling authentication" }
        write(values)
    }

    fun browserSnapshot() = browser().let { b ->
        buildJsonObject { put("enabled", b.enabled); put("port", b.port); put("auth", b.apiKey != null) }
    }

    /** Settings executed by the desktop adapters: tray, notifications, download windows and the autostart gate. */
    fun integration() = buildJsonObject {
        for ((key, default) in integrationDefaults) put(key, effective(key, default.jsonPrimitive) { value(key, it) })
    }

    fun updateIntegration(patch: JsonObject) {
        require(patch.isNotEmpty() && patch.keys.all { it in integrationDefaults }) { "Unsupported integration setting" }
        write(patch.mapValues { (key, element) ->
            require(element is JsonPrimitive) { "Invalid $key" }
            value(key, element).also {
                // Like the original picker: an existing WAV file, or empty for the default sound.
                if (key in notificationSoundKeys && it.content.isNotEmpty()) require(it.content.endsWith(".wav", ignoreCase = true)
                    && java.nio.file.Files.isRegularFile(java.nio.file.Path.of(it.content))) { "Notification sound must be an existing .wav file" }
            }
        })
    }

    /** Configured sound file for a notice, or null for the default sound (also when it no longer exists). */
    fun notificationSoundFile(key: String): String? = integration()[key]?.jsonPrimitive?.content
        ?.takeIf { it.isNotEmpty() && java.nio.file.Files.isRegularFile(java.nio.file.Path.of(it)) }

    /** Original BaseAppRepository.shouldUseSpeedLimit, with an explicit activity adapter. */
    fun speedLimitActive(): Boolean = when (snapshot().getValue("useSpeedLimit").jsonPrimitive.content) {
        "Enabled" -> true
        "EnabledWhenBessy" -> activity.isUserInteracting() && activity.isFrontendInBackground()
        else -> false
    }

    fun speedLimitMode(): String = snapshot().getValue("useSpeedLimit").jsonPrimitive.content

    /** Read-only, secret-free view of inactive original settings and the speed-limit adapter state. */
    fun policySnapshot() = buildJsonObject {
        put("retained", OriginalAppSettings.retained(document))
        put("activityAdapter", activity.selectable)
        put("activityReady", activity.available)
        put("speedLimitActive", speedLimitActive())
        put("corrupt", JsonArray(corruptKeys().map(::JsonPrimitive)))
    }

    fun policy(): DownloadEnginePolicy {
        val v = snapshot()
        return DownloadEnginePolicy(
            threadCount = v.getValue("threadCount").jsonPrimitive.int,
            maxDownloadRetryCount = v.getValue("maxDownloadRetryCount").jsonPrimitive.int,
            minPartSize = v.getValue("minPartSize").jsonPrimitive.content.toLong(),
            dynamicPartCreation = v.getValue("dynamicPartCreation").jsonPrimitive.boolean,
            useServerLastModifiedTime = v.getValue("useServerLastModifiedTime").jsonPrimitive.boolean,
            useSparseFileAllocation = v.getValue("useSparseFileAllocation").jsonPrimitive.boolean,
            // Boot-only legacy setting remains non-writable through this subset.
            appendExtensionToIncompleteDownloads = effective("appendExtensionToIncompleteDownloads", JsonPrimitive(false)) {
                value("appendExtensionToIncompleteDownloads", it)
            }.boolean,
            globalSpeedLimit = DownloadEnginePolicy.effectiveSpeedLimit(
                speedLimitActive(),
                v.getValue("speedLimit").jsonPrimitive.content.toLong(),
            ),
            maxConcurrentDownloads = v.getValue("maxConcurrentDownloads").jsonPrimitive.int,
            useAverageSpeed = v.getValue("useAverageSpeed").jsonPrimitive.boolean,
        )
    }
}
