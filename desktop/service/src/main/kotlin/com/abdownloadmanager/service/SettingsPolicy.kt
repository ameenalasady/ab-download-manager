package com.abdownloadmanager.service

import kotlinx.serialization.json.*

/** How the isolated service treats each property of the original flat appSettings.json.
 * Every key of the original desktop AppSettingsModel must be classified; unknown/future
 * keys are retained losslessly and never interpreted.
 */
enum class SettingPolicy(val wire: String) {
    /** Validated, writable, applied to the existing engine through DownloadEnginePolicy. */
    Engine("engine"),
    /** Applied to the engine at boot only; retained, never writable through this service. */
    EngineBootOnly("engine-boot"),
    /** Writable presentation preference with no side effects; interpreted by the frontend. */
    Display("display"),
    /** Desktop/notification integration or an automatic destructive action. Retained, reported, never executed. */
    Platform("platform"),
    /** Original browser-integration HTTP API. Executed by [BrowserBridge] in explicit-runtime mode only. */
    Browser("browser"),
    /** Executed by the desktop adapters: tray and per-download progress/completion windows
     * (frontend), notification sound (service) and the installed autostart entry's gate
     * (launcher). Never writes desktop autostart files. */
    Integration("integration"),
    /** Superseded by the explicit runtime policy (destination roots, TLS, identity). Retained, never honored. */
    RuntimeOverride("runtime-override"),
    /** Honored only inside the explicit runtime's destination roots (default download folder). */
    Destination("destination"),
    /** Compose toolkit appearance. Retained for the original app; the Qt frontend follows the desktop theme. */
    Toolkit("toolkit"),
}

object OriginalAppSettings {
    val policies: Map<String, SettingPolicy> = linkedMapOf(
        "theme" to SettingPolicy.Toolkit, "defaultDarkTheme" to SettingPolicy.Toolkit,
        "defaultLightTheme" to SettingPolicy.Toolkit, "language" to SettingPolicy.Toolkit,
        "font" to SettingPolicy.Toolkit, "uiScale" to SettingPolicy.Toolkit,
        "showIconLabels" to SettingPolicy.Toolkit, "useRelativeDateTime" to SettingPolicy.Toolkit,
        "mergeTopBarWithTitleBar" to SettingPolicy.Toolkit, "useNativeMenuBar" to SettingPolicy.Toolkit,
        "threadCount" to SettingPolicy.Engine, "maxConcurrentDownloads" to SettingPolicy.Engine,
        "maxDownloadRetryCount" to SettingPolicy.Engine, "dynamicPartCreation" to SettingPolicy.Engine,
        "useServerLastModifiedTime" to SettingPolicy.Engine, "useSparseFileAllocation" to SettingPolicy.Engine,
        "useAverageSpeed" to SettingPolicy.Engine, "useSpeedLimit" to SettingPolicy.Engine,
        "speedLimit" to SettingPolicy.Engine, "minPartSize" to SettingPolicy.Engine,
        "appendExtensionToIncompleteDownloads" to SettingPolicy.EngineBootOnly,
        "sizeUnit" to SettingPolicy.Display, "speedUnit" to SettingPolicy.Display,
        // Add-form default only: Automatic (true) versus No Category (false).
        "useCategoryByDefault" to SettingPolicy.Display,
        // Original onNewDownloadEvent: windows for user-started downloads, opened by the frontend.
        "showDownloadProgressDialog" to SettingPolicy.Integration, "showDownloadCompletionDialog" to SettingPolicy.Integration,
        "autoStartOnBoot" to SettingPolicy.Integration, "notificationSound" to SettingPolicy.Integration,
        // Passed as the notification's sound-file hint (honored by Plasma 6.4+).
        "generalNotificationSound" to SettingPolicy.Integration, "errorNotificationSound" to SettingPolicy.Integration,
        "successNotificationSound" to SettingPolicy.Integration, "apiEnabled" to SettingPolicy.Browser,
        "apiPort" to SettingPolicy.Browser, "apiAuthKey" to SettingPolicy.Browser,
        "apiAuthEnabled" to SettingPolicy.Browser, "useSystemTray" to SettingPolicy.Integration,
        // Both remove records/files automatically in the original; never run here.
        "trackDeletedFilesOnDisk" to SettingPolicy.Platform,
        "deletePartialFileOnDownloadCancellation" to SettingPolicy.Platform,
        "defaultDownloadFolder" to SettingPolicy.Destination, "ignoreSSLCertificates" to SettingPolicy.RuntimeOverride,
        "userAgent" to SettingPolicy.RuntimeOverride,
    )

    /** Values the frontend may display. Secrets and free-form strings (paths, keys, sound
     * files, user agents) are reported only as present/absent, never sent. */
    private val reportedValues = setOf("autoStartOnBoot",
        "notificationSound", "useSystemTray", "trackDeletedFilesOnDisk",
        "deletePartialFileOnDownloadCancellation", "ignoreSSLCertificates")

    val sizeUnits = listOf("BinaryBits", "BinaryBytes", "DecimalBits", "DecimalBytes")

    /** Retained-but-inactive original settings, for an honest read-only view. */
    fun retained(document: JsonObject): JsonArray = JsonArray(policies.filterValues {
        it == SettingPolicy.Platform || it == SettingPolicy.RuntimeOverride
    }.map { (key, policy) ->
        val stored = document[key]
        buildJsonObject {
            put("key", key); put("policy", policy.wire); put("stored", stored != null && stored !is JsonNull)
            val primitive = stored as? JsonPrimitive
            put("value", when {
                key !in reportedValues || primitive == null || primitive.isString -> JsonNull
                primitive.booleanOrNull != null -> JsonPrimitive(primitive.boolean)
                primitive.intOrNull != null -> JsonPrimitive(primitive.int)
                else -> JsonNull
            })
        }
    })
}

/** Inputs of the original activity-dependent ("EnabledWhenBessy") speed limit. */
interface SystemActivity {
    /** Recent system-wide user input. */
    fun isUserInteracting(): Boolean
    /** No frontend window is active. */
    fun isFrontendInBackground(): Boolean
    val available: Boolean
    /** A configured monitor can be selected before its first idle event arrives. */
    val selectable: Boolean get() = available
}

/** No idle-time or focus adapter was configured. Mirrors the original
 * SystemUsageMonitor.defaultInteractingWhenIdleTimeNotSupportedByOS(): "not interacting",
 * so the activity-dependent limit evaluates to unlimited, exactly as the original does
 * when the OS cannot report idle time. Nothing is guessed from the frontend.
 */
object UnsupportedSystemActivity : SystemActivity {
    override fun isUserInteracting() = false
    override fun isFrontendInBackground() = true
    override val available = false
}
