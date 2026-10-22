package com.abdownloadmanager.service

import com.abdownloadmanager.shared.util.category.*
import com.abdownloadmanager.integration.IntegrationHandler
import com.abdownloadmanager.integration.model.AddDownloadsFromIntegration
import com.abdownloadmanager.integration.model.ApiQueueModel
import com.abdownloadmanager.integration.model.NewDownloadTask
import com.abdownloadmanager.shared.storage.ExtraDownloadSettingsStorage
import com.abdownloadmanager.shared.storage.ExtraQueueSettingsStorage
import com.abdownloadmanager.shared.util.DownloadFoldersRegistry
import com.abdownloadmanager.shared.util.DownloadSystem
import okio.Path.Companion.toOkioPath
import com.abdownloadmanager.shared.util.DownloadEnginePolicyController
import com.abdownloadmanager.shared.util.downloaderror.DownloadErrorMapperRegistry
import com.abdownloadmanager.shared.util.downloaderror.faileddownloads.*
import com.abdownloadmanager.shared.util.ondownloadcompletion.*
import com.abdownloadmanager.shared.util.onqueuecompletion.*
import kotlinx.serialization.builtins.ListSerializer
import ir.amirab.downloader.*
import ir.amirab.downloader.connection.*
import ir.amirab.downloader.connection.proxy.*
import ir.amirab.downloader.db.*
import ir.amirab.downloader.downloaditem.*
import ir.amirab.downloader.downloaditem.contexts.ResumedBy
import ir.amirab.downloader.downloaditem.http.*
import ir.amirab.downloader.utils.*
import ir.amirab.downloader.queue.*
import ir.amirab.downloader.monitor.*
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.sync.withLock
import java.util.concurrent.Executors
import kotlinx.coroutines.*
import kotlinx.serialization.json.*
import kotlinx.serialization.modules.*
import okhttp3.OkHttpClient
import java.io.File
import java.nio.file.Path

/** Toolkit-neutral owner of the EXISTING core engine and transactional stores.
 * This isolated composition root adapts original settings/categories, but is not a production composition.
 * No Compose, AWT, Koin, tray or power actions. The original browser-integration HTTP API
 * runs only when [browserIntegration] is set (explicit runtime by default, never isolated).
 */
class EngineSession(
    private val root: Path, private val runtime: RuntimePolicy = RuntimePolicy.isolated(root),
    browserIntegration: Boolean = !runtime.isolated, private val launcher: FrontendLauncher = FrontendLauncher(null),
    private val notifier: DesktopNotifier = NoDesktopNotifier, activityCommand: List<String>? = null,
) {
    init { ProfileCopies.requireNotQuarantined(root) }
    private val foldersRegistry = DownloadFoldersRegistry()
    private val data = root.resolve("app-data").toFile()
    private fun directory(path: String) = foldersRegistry.registerAndGet(data.resolve(path).toOkioPath())
        .apply { check(mkdirs() || isDirectory) }
    private val client = OkHttpClient.Builder().proxy(java.net.Proxy.NO_PROXY)
        .followRedirects(false).followSslRedirects(false)
        .addInterceptor(runtime.redirectInterceptor()).addNetworkInterceptor(runtime.networkInterceptor()).build()
    private val http = OkHttpHttpDownloaderClient(client,
        object : UserAgentProvider { override fun getUserAgent() = "ABDM-Qt-Migration/1" },
        object : ProxyStrategyProvider { override fun getProxyStrategyFor(url: String) = runtime.proxyFor(url) },
        NoopSystemProxySelectorProvider(), AutoConfigurableProxyProvider.NoOp())
    private val json = Json {
        encodeDefaults = true
        serializersModule = SerializersModule {
            polymorphic(IDownloadItem::class) { subclass(HttpDownloadItem::class, HttpDownloadItem.serializer()) }
        }
    }
    private val saver = TransactionalFileSaver(json)
    init { validateStoredDownloads(data.resolve("config/download_db"), json) }
    private val list = DownloadListFileStorage(directory("config/download_db/downloadlist"), saver)
    private val parts = PartListFileStorage(directory("config/download_db/parts"), saver)
    private val settings = DownloadSettings()
    private val activity = MonitoredSystemActivity(activityCommand, launcher) { evaluateActivitySoon() }
    private val preferences = EngineSettingsRepository(data.resolve("config/appSettings.json"), saver, activity)
    // Finish fallible filesystem preparation before creating dispatchers/monitor listeners.
    // A constructor that throws cannot be assigned to ServiceLifecycle for joined cleanup.
    private val systemDownloads = directory("system/downloadData")
    private val queueDirectory = directory("config/download_db/queues")
    private val downloads = runtime.destination.toFile().apply {
        if (runtime.isolated) check(mkdirs() || isDirectory)
        runtime.validateFolder(toPath())
    }
    private val categoryFile = directory("config/download_db/categories").resolve("categories.json")
    private val extraDownloads = ExtraDownloadSettingsStorage(directory("config/download_db/extra_download_settings"), saver, StoredExtraSettings, strictReads = true)
    private val extraQueues = ExtraQueueSettingsStorage(directory("config/download_db/extra_queue_settings"), saver, StoredExtraSettings, strictReads = true)
    private val errorRegistry = DownloadErrorMapperRegistry(listOf(serviceFallbackErrorMapper()))
    // Fallible journal validation must precede thread-owning components.
    private val intake = BrowserIntake(journalFile = data.resolve("config/browser-review.json").toPath())
    private val addGate = kotlinx.coroutines.sync.Mutex()
    // Queue collections and their scheduler/listener/autosaver run on ONE owner thread.
    private val context = Executors.newSingleThreadExecutor { r -> Thread(r, "service-owner") }.asCoroutineDispatcher()
    private val queueScope = CoroutineScope(SupervisorJob() + context)
    private val registry = DownloaderRegistry().apply { add(HttpDownloader(lazy { http })) }
    private val manager = DownloadManager(list, parts, settings,
        EmptyFileCreator(object : IDiskStat { override fun getRemainingSpace(path: File) = path.usableSpace },
            { settings.useSparseFileAllocation }), registry, systemDownloads,
        destinationValidator = { runtime.validateDestination(it.toPath()) })
    private val queueStore = DownloadQueueFileStorageDatabase(saver, queueDirectory)
    private val queues = QueueManager(queueStore, manager, queueScope.coroutineContext)
    private val manual = ManualDownloadQueue(manager, queueScope)
    private val monitor = DownloadMonitor(manager, manual, lazy {
        object : DownloadItemStateFactory<IDownloadItem, DownloadJob> {
            override fun createProcessingDownloadItemState(props: ProcessingDownloadItemFactoryInputs<DownloadJob>) =
                createHttpProcessingState(ProcessingDownloadItemFactoryInputs(props.downloadJob as HttpDownloadJob, props.speed, props.isWaiting))
            override fun createCompletedDownloadItemState(downloadItem: IDownloadItem) = CompletedDownloadItemState.fromDownloadItem(downloadItem)
        }
    }, queueScope.coroutineContext)
    private val policyController = DownloadEnginePolicyController(settings, manager, manual, monitor)
    private var monitorSubscription: Job? = null
    private val failureStorage = FailedDownloadErrorStorageInMemory()
    private val failures = FailedDownloads(manager, failureStorage, errorRegistry, queueScope)
    // Explicit isolated policy: never execute desktop, notification, opener or power actions.
    private val completion = OnDownloadCompletionActionRunner(manager, queueScope, NoOpOnDownloadCompletionActionProvider())
    private val queueCompletion = OnQueueEventActionRunner(queues, queueScope, NoopOnQueueCompletionActionProvider())
    // Keep the original MyIcons URI mapping (including its historical property names).
    private val categoryIcons = listOf("icon:pictureFile", "icon:zipFile", "icon:musicFile",
        "icon:folderOpen", "icon:fileOpen", "icon:videoFile")
    private val categories = CategoryManager(CategoryFileStorage(categoryFile, saver), queueScope,
        CategoryDefaults {
            StandardCategoryDefaults(categoryIcons) { downloads.path }.getDefaultCategories()
                .map { it.copy(path = downloads.path, usePath = false) }
        }, DownloadManagerCategoryItemProvider(manager))
    private val facade = DownloadSystem(manager, queues, manual, categories, monitor,
        completion, queueCompletion, errorRegistry, failures, queueScope, list,
        extraQueues, extraDownloads, failureStorage, foldersRegistry)
    private val ownedJobs = mutableSetOf<DownloadJob>()
    private val integrationJson = Json { ignoreUnknownKeys = true }
    private val browserHandler = object : IntegrationHandler {
        override suspend fun addDownloadByGui(request: AddDownloadsFromIntegration) { browserAdd(request) }
        override fun listQueues() = runBlocking(context) {
            queues.getAll().map { ApiQueueModel(it.id, it.getQueueModel().name) }
        }
        override suspend fun addDownload(task: NewDownloadTask): Long = withContext(context) { headlessAdd(task) }
    }
    private val bridge = BrowserBridge(browserHandler, browserIntegration)
    private var revision = 0L
    private val history = BoundedHistory()
    private var lastSnapshot: JsonObject? = null
    private var eventSubscription: Job? = null
    private var activityTicker: Job? = null
    private var limitActive: Boolean? = null
    // Latest request to show the window: serial, xdg-activation token, action, target.
    private var activationSerial = 0L
    private var activation: JsonObject? = null
    // Presentation hints for per-download windows (original onNewDownloadEvent). Owner thread,
    // memory-only and bounded; never commands, never replayed. A frontend baselines on connect.
    private var windowSerial = 0L
    private val windowEvents = ArrayDeque<JsonObject>()
    companion object {
        const val MAX_WINDOW_EVENTS = 32
        const val MAX_PART_DOWNLOADS = 16
        const val MAX_PARTS = 256
        val capabilities = listOf("snapshot", "add", "pause", "resume", "settings.update", "queue.create",
            "queue.update", "queue.schedule", "queue.add", "queue.remove", "queue.move", "queue.start", "queue.stop", "queue.delete",
            "category.create", "category.update", "category.delete", "category.move", "category.assign", "monitor.stats", "settings.speed",
            "download.details", "download.forget", "download.edit", "download.http-options", "runtime.policy", "service.shutdown",
            "settings.display", "settings.policy", "browser.integration", "browser.intake", "browser.add", "browser.accept",
            "browser.dismiss", "browser.settings", "settings.integration", "frontend.activate", "links.import",
            "system.activity", "download.probe", "download.review", "history.page", "history.delta", "download.windows", "download.parts",
            "download.replace", "settings.folder", "settings.duplicates")
    }

    /** A frontend polled; browser requests then wait for it instead of launching one.
     * [active]: its window has focus (the original background tracker's input). */
    fun frontendSeen(active: Boolean = false) {
        val before = launcher.active
        launcher.seen(active)
        if (before != launcher.active) evaluateActivitySoon()
    }

    private fun evaluateActivitySoon() { queueScope.launch { evaluateActivity() } }

    /** Re-applies the engine policy when the activity-dependent limit flips. Owner thread. */
    private fun evaluateActivity() {
        val active = preferences.speedLimitActive()
        if (active != limitActive) { limitActive = active; policyController.apply(preferences.policy()) }
    }

    /** The helper runs only while the activity-dependent mode is selected. Owner thread. */
    private fun updateActivityMonitor() {
        val wanted = preferences.speedLimitMode() == "EnabledWhenBessy"
        activity.wanted(wanted)
        activityTicker?.cancel(); activityTicker = null
        // Frontend disconnects have no event; a slow tick notices the lost focus report.
        if (wanted) activityTicker = queueScope.launch { while (isActive) { evaluateActivity(); delay(1000) } }
        evaluateActivity()
    }

    /** Ask the frontend to show itself (launching one if none is connected). Owner thread. */
    private fun requestActivation(token: String?, action: String, downloadId: Long? = null) {
        activation = buildJsonObject {
            put("serial", (++activationSerial).toString()); put("action", action)
            put("token", token?.let(::JsonPrimitive) ?: JsonNull)
            put("downloadId", downloadId?.let { JsonPrimitive(it.toString()) } ?: JsonNull)
        }
        if (!launcher.connected) launcher.request(token, explicit = true)
    }

    /** Original AppComponent.onNewDownloadEvent: only downloads the USER started notify. */
    private fun onEngineEvent(event: DownloadManagerEvents) {
        if (event.context[ResumedBy]?.by !is ir.amirab.downloader.downloaditem.contexts.User) return
        val sound = preferences.integration().getValue("notificationSound").jsonPrimitive.boolean
        val item = event.downloadItem
        when (event) {
            is DownloadManagerEvents.OnJobStarting -> windowEvent("started", item.id)
            is DownloadManagerEvents.OnJobCompleted -> {
                windowEvent("completed", item.id)
                notifier.notify(DesktopNotice("download:${item.id}", item.name, "Finished", false, sound, "show", item.id))
            }
            // A normal pause cancels without a mapped reason: no error window or notice.
            is DownloadManagerEvents.OnJobCanceled -> errorRegistry.getReason(event.e)?.let { reason ->
                windowEvent("failed", item.id)
                notifier.notify(DesktopNotice("download:${item.id}", item.name, reason.title, true, sound, "show", item.id))
            }
            else -> {}
        }
    }

    /** Owner thread. Whether a window opens is the frontend's decision (its settings). */
    private fun windowEvent(kind: String, id: Long) {
        windowEvents.addLast(buildJsonObject { put("serial", (++windowSerial).toString()); put("kind", kind); put("id", id.toString()) })
        while (windowEvents.size > MAX_WINDOW_EVENTS) windowEvents.removeFirst()
    }

    /** Browser `/add` and native-host `add`: silent requests are added (and optionally
     * started) directly, as in the original; others wait for the frontend's decision.
     */
    suspend fun browserAdd(request: AddDownloadsFromIntegration): String? = withContext(context) {
        if (request.options.silentAdd) {
            val items = request.items.map { BrowserLinks.item(it, runtime) }
            items.forEachIndexed { index, item ->
                val id = try { addOne(item.url, item.name, null, "auto", item.options(JsonObject(emptyMap())), item.downloadPage) }
                catch (e: DownloadAddedButCategoryFailed) { throw e }
                catch (e: Exception) { if (index == 0) throw e else throw BrowserAddPartiallyApplied(index, items.size, e) }
                if (request.options.silentStart) facade.userManualResume(id)
            }
            null
        } else intake.offer(request, runtime, System.currentTimeMillis()).let { pending ->
            launcher.request()
            // A window that exists but lacks focus may not be raised without a token (KWin
            // focus-stealing prevention). Clicking the notification provides one.
            if (launcher.connected && !launcher.active) notifier.notify(DesktopNotice("browser-request",
                "Download request from browser", pending.items.joinToString(", ") { it.name }.take(240),
                false, preferences.integration().getValue("notificationSound").jsonPrimitive.boolean, "review"))
            pending.id
        }
    }

    /** Original `/start-headless-download`: explicit folder/name/queue/category, no GUI. */
    private suspend fun headlessAdd(task: NewDownloadTask): Long {
        val item = BrowserLinks.item(task.downloadSource, runtime)
        val name = task.name ?: item.name
        val queueId = task.queueId?.also { id -> require(queues.getAll().any { it.id == id }) { "Queue no longer exists" } }
        val id = addOne(item.url, name, task.folder?.let(Path::of), task.categoryId?.toString() ?: "auto",
            item.options(JsonObject(emptyMap())), item.downloadPage, queueId)
        // Either start the queue or the download, never both (original behavior).
        if (task.startQueue && queueId != null) queues.getQueue(queueId).start()
        else if (task.startDownload) facade.userManualResume(id)
        return id
    }

    /** Shared Add path: validation, record, then the separately persisted category. */
    private suspend fun addOne(url: String, name: String, folderOverride: Path?, selection: String, options: JsonObject,
                               downloadPage: String? = null, queueId: Long? = null): Long = addGate.withLock {
        runtime.validateUrl(url)
        require(name.length in 1..180 && name != "." && name != ".." &&
            name.none { it == '/' || it == '\\' || it.isISOControl() }) { "Invalid file name" }
        // Resolve/validate selection before adding a record. "auto" uses the same
        // extension + URL specificity policy as the original DownloadSystem facade.
        val category = when (selection) {
            "auto" -> categories.getCategoryOf(CategoryItem(name, url))
            "none" -> null
            else -> categories.getCategoryById(selection.toLongOrNull() ?: error("Invalid category ID"))
                ?: error("Category no longer exists")
        }
        val folder = folderOverride ?: destination().toPath()
        runtime.validateFolder(folder)
        val duplicatePolicy = options["duplicatePolicy"]?.jsonPrimitive?.content ?: "numbered"
        require(duplicatePolicy in setOf("reject", "numbered", "overwrite")) { "Unsupported duplicate policy" }
        if (duplicatePolicy == "reject") {
            require(manager.getDownloadList().none { it.link == url || (it.folder == folder.toString() && it.name == name) } &&
                !java.nio.file.Files.exists(folder.resolve(name), java.nio.file.LinkOption.NOFOLLOW_LINKS)) {
                "Duplicate URL, record destination or existing file; review without overwriting"
            }
        }
        val draft = HttpDownloadItem(link = url, id = -1, folder = folder.toString(), name = name, downloadPage = downloadPage)
        applyHttpOptions(draft, options)
        val replaced = if (duplicatePolicy == "overwrite") replaceDestination(folder, name) else emptyList()
        val id = try {
            facade.addDownload(NewDownloadItemProps(
                draft,
                extraConfig = null, onDuplicateStrategy = OnDuplicateStrategy.AddNumbered, context = EmptyContext),
                queueId = queueId, categoryId = null)
        } catch (e: CancellationException) { throw e }
        catch (e: Exception) {
            if (replaced.isNotEmpty()) throw DownloadReplacePartiallyApplied(replaced.first(), e)
            throw e
        }
        ownedJobs.addAll(manager.downloadJobs)
        category?.let {
            val before = categories.getCategories()
            try { categories.addItemsToCategory(it.id, listOf(id)); categories.persistNow() }
            catch (e: Exception) {
                categories.setCategories(before)
                // The existing record and category stores are not one cross-file transaction.
                // Never label a committed Add as a clean rejection or silently retry it.
                throw DownloadAddedButCategoryFailed(id, e)
            }
        }
        id
    }

    /** The original OnDuplicateStrategy.OverrideDownload, made explicit: only records with the
     * SAME destination are replaced, with their queue/category membership and files. Refused
     * while any of them is active or in a running queue, so no transfer is cut off. Downloads
     * that merely share the URL, and files without a record, are never touched. */
    private suspend fun replaceDestination(folder: Path, name: String): List<Long> {
        val targets = manager.getDownloadList().filter { it.folder == folder.toString() && it.name == name }
        for (item in targets) {
            require(item.status != DownloadStatus.Downloading && manager.getJobStatusOf(item.id) !is DownloadJobStatus.IsActive &&
                manager.downloadJobs.none { it.id == item.id && it.isDownloadActive.value }) { "Pause the existing download before replacing it" }
            require(queues.getAll().none { it.isQueueActive && item.id in it.getQueueModel().queueItems }) {
                "Stop queues containing the existing download before replacing it"
            }
        }
        for (item in targets) {
            val memberships = queues.getAll().filter { item.id in it.getQueueModel().queueItems }
            manual.removeFromQueue(item.id)
            memberships.forEach { it.removeFromQueue(item.id) }
            try {
                memberships.forEach { it.persistNow() }
                facade.removeDownload(item.id, alsoRemoveFile = true, context = EmptyContext)
                categories.persistNow()
            } catch (e: CancellationException) { throw e }
            catch (e: Exception) { throw DownloadReplacePartiallyApplied(item.id, e) }
            finally {
                ownedJobs.removeAll { it.id == item.id && it.scope.coroutineContext.job.isCompleted }
                ownedJobs.addAll(manager.downloadJobs)
            }
        }
        return targets.map { it.id }
    }

    /** Default destination for new downloads: the stored original defaultDownloadFolder when
     * it is (still) an existing writable folder inside the runtime roots, else the runtime's. */
    private fun destination(): java.io.File = preferences.defaultFolder()
        ?.let { runCatching { Path.of(it).also(runtime::validateFolder).toFile() }.getOrNull() } ?: downloads

    /** Captured browser headers are the base; user-supplied options override them. */
    private fun BrowserItem.options(user: JsonObject): JsonObject {
        val captured = credentials ?: return user
        val supplied = user["credentials"]?.jsonObject
        val headers = buildJsonObject {
            captured["headers"]?.jsonObject?.forEach { (k, v) ->
                if (supplied?.get("headers")?.jsonObject?.keys?.none { it.equals(k, true) } != false) put(k, v)
            }
            supplied?.get("headers")?.jsonObject?.forEach { (k, v) -> put(k, v) }
        }
        val credentials = buildJsonObject {
            supplied?.forEach { (k, v) -> if (k != "headers") put(k, v) }
            for (key in listOf("userAgent", "username", "password")) {
                if (supplied?.get(key) == null) captured[key]?.let { put(key, it) }
            }
            put("headers", headers)
        }
        return JsonObject(user + ("credentials" to credentials))
    }

    suspend fun boot() = withContext(context) {
        preferences.applyTo(settings)
        manager.limitGlobalSpeed(settings.globalSpeedLimit)
        // Core queue storage skips unreadable models: fail closed before any scheduler boots.
        queueStore.getAllQueueIds().forEach { id ->
            val q = queueStore.getQueue(id)
            require(q.id == id && q.maxConcurrent in 1..1024) { "Invalid persisted queue" }
        }
        // Never trust stored URLs/destinations merely because a directory is inside build/.
        list.getAll().forEach {
            runtime.validateUrl(it.link)
            runtime.validateFolder(Path.of(it.folder))
            require(it is HttpDownloadItem) { "Unsupported stored download type" }
            validateHttpOptions(it)
            require(it.name.length in 1..180 && it.name != "." && it.name != ".." &&
                it.name.none { c -> c == '/' || c == '\\' || c.isISOControl() }) { "Invalid stored file name" }
        }
        intake.requests.forEach { r -> r.items.forEach { runtime.validateUrl(it.url) } }
        // Core's forgiving storage reader must not turn corrupt category data into an empty
        // list and overwrite it during shutdown. Validate before starting any category writer.
        if (categoryFile.exists()) {
            require(categoryFile.length() <= 2 * 1024 * 1024) { "Category document too large" }
            validateCategories(json.decodeFromString(ListSerializer(Category.serializer()), categoryFile.readText()))
        }
        // Do not silently default corrupt, mismatched or unfinished extra-settings
        // transactions and then delete them through facade cleanup.
        extraDownloads.validateStored(); extraQueues.validateStored()
        facade.boot(); ownedJobs.addAll(manager.downloadJobs)
        categories.persistNow()
        policyController.apply(preferences.policy())
        monitorSubscription = queueScope.launch { monitor.activeDownloadListFlow.collect() }
        eventSubscription = queueScope.launch { facade.downloadEvents.collect { onEngineEvent(it) } }
        notifier.onActivated = { notice, token ->
            queueScope.launch { requestActivation(token, notice.action, notice.downloadId) }
        }
        updateActivityMonitor()
        bridge.apply(preferences.browser())
    }

    /** Tests and orderly startup: wait until the browser server bound or reported failure. */
    fun awaitBrowserBridge() = bridge.awaitIdle()

    /** Separate summary-only API: existing Qt snapshot consumer is not silently given
     * a partial history or fictitious global counts. Frontend paging is still pending. */
    suspend fun historyPage(request: JsonObject): JsonObject = withContext(context) {
        val items = manager.getDownloadList()
        require(items.size <= 50_000) { "History exceeds bounded-query fixture limit" }
        val rows = items.sortedWith(compareByDescending<IDownloadItem> { it.dateAdded }.thenByDescending { it.id }).map { item -> buildJsonObject {
            put("id", item.id.toString()); put("name", item.name); put("size", item.contentLength.toString())
            put("dateAdded", item.dateAdded.toString()); put("state", item.status.name)
        } }
        history.query(rows, request)
    }

    /** Read-only, on-demand part projection. Only requested stable IDs (max 16),
     * max 256 ranges each. No URLs, paths, headers, exception text or credentials.
     * Completed jobs retire their parts: report unavailable, never synthesize them.
     * Volatile range endpoints can change as the engine splits; bound each sample. */
    private fun partSnapshot(ids: List<Long>): JsonObject = buildJsonObject {
        require(ids.size <= MAX_PART_DOWNLOADS && ids.distinct().size == ids.size && ids.all { it >= 0 }) { "Invalid part selection" }
        for (id in ids) {
            val job = manager.downloadJobs.find { it.id == id } as? HttpDownloadJob
            val ranges = job?.getParts().orEmpty()
            put(id.toString(), buildJsonObject {
                put("available", job != null && ranges.isNotEmpty())
                put("totalCount", ranges.size); put("truncated", ranges.size > MAX_PARTS)
                put("resumeSupport", job?.supportsConcurrent?.let(::JsonPrimitive) ?: JsonNull)
                put("items", JsonArray(ranges.take(MAX_PARTS).map { part ->
                    val from = part.from
                    val end = part.to
                    val length = end?.let { (it - from + 1).coerceAtLeast(0) }
                    val done = (part.current - from).coerceAtLeast(0).let { if (length != null) it.coerceAtMost(length) else it }
                    buildJsonObject {
                        put("id", from.toString()); put("offset", from.toString())
                        put("length", length?.let { JsonPrimitive(it.toString()) } ?: JsonNull)
                        put("downloaded", done.toString())
                        put("state", when (part.status) {
                            ir.amirab.downloader.part.PartDownloadStatus.IDLE -> "idle"
                            ir.amirab.downloader.part.PartDownloadStatus.Connecting -> "connecting"
                            ir.amirab.downloader.part.PartDownloadStatus.ReceivingData -> "downloading"
                            ir.amirab.downloader.part.PartDownloadStatus.Completed -> "completed"
                            is ir.amirab.downloader.part.PartDownloadStatus.Canceled -> "disconnected"
                        })
                    }
                }))
            })
        }
    }

    suspend fun snapshot(partsFor: List<Long> = emptyList()): JsonObject = withContext(context) {
        val measurements = monitor.activeDownloadListFlow.value.associateBy { it.id }
        val jobs = manager.downloadJobs.associateBy { it.id }
        val rows = JsonArray(manager.getDownloadList().sortedByDescending { it.dateAdded }.map { item ->
            val job = jobs[item.id]
            val done = if (item.status == DownloadStatus.Completed) item.contentLength.coerceAtLeast(0)
                else job?.getDownloadedSize() ?: 0
            buildJsonObject {
                put("id", item.id.toString()) // Never send 64-bit IDs as JSON numbers to QML/JS.
                put("name", item.name); put("size", item.contentLength.toString()); put("downloaded", done.toString())
                put("dateAdded", item.dateAdded.toString())
                put("details", buildJsonObject {
                    put("url", item.link); put("folder", item.folder)
                    val httpItem = item as HttpDownloadItem
                    put("connections", httpItem.preferredConnectionCount?.let(::JsonPrimitive) ?: JsonNull)
                    put("speedLimit", httpItem.speedLimit.toString())
                    put("hasCredentials", !httpItem.username.isNullOrEmpty() || !httpItem.headers.isNullOrEmpty() || !httpItem.userAgent.isNullOrEmpty())
                    // Original reasons are intentionally memory-only. After restart an Error
                    // row may have no reason; never invent diagnostics from status alone.
                    val reason = failureStorage.reasons.value[item.id]
                    put("error", if (reason == null) JsonNull else buildJsonObject {
                        put("title", reason.title.take(512)); put("description", reason.description.take(8192))
                        put("suggestion", reason.suggestion.take(8192)); put("throwableName", reason.throwableName.take(512))
                        put("throwableMessage", reason.throwableMessage?.take(8192)?.let(::JsonPrimitive) ?: JsonNull)
                    })
                })
                val measurement = measurements[item.id]
                val active = item.status == DownloadStatus.Downloading
                put("speed", if (active) (measurement?.speed ?: -1).toString() else "0")
                put("remainingTime", if (active) (measurement?.remainingTime?.coerceAtLeast(0) ?: -1).toString() else "-1")
                put("state", when (item.status) {
                    DownloadStatus.Completed -> "completed"; DownloadStatus.Error -> "failed"
                    DownloadStatus.Paused -> "paused"; DownloadStatus.Downloading -> "downloading"
                    DownloadStatus.Added -> "queued"
                })
            }
        })
        val state = buildJsonObject {
            put("state", "ready"); put("rows", rows); put("settings", preferences.snapshot())
            put("downloadParts", partSnapshot(partsFor))
            put("settingsPolicy", preferences.policySnapshot())
            put("browserIntegration", bridge.snapshot())
            put("browserRequests", intake.snapshot())
            put("integration", buildJsonObject {
                preferences.integration().forEach { (k, v) -> put(k, v) }
                put("notifications", notifier.available)
            })
            put("activity", buildJsonObject {
                activity.snapshot().forEach { (key, value) -> put(key, value) }
                put("appliedSpeedLimit", settings.globalSpeedLimit.toString())
            })
            put("frontend", buildJsonObject { put("activation", activation ?: JsonNull) })
            put("downloadWindows", buildJsonObject {
                put("serial", windowSerial.toString()); put("events", JsonArray(windowEvents.toList()))
            })
            put("runtime", buildJsonObject {
                put("mode", runtime.mode); put("destination", destination().path)
                put("destinationRoots", JsonArray(runtime.destinationRoots.map { JsonPrimitive(it.toString()) }))
                put("redirects", runtime.redirects); put("proxy", if (runtime.usesExplicitProxy) "explicit" else "direct")
            })
            put("manualPending", JsonArray(manual.pendingItems.value.map { JsonPrimitive(it.toString()) }))
            put("categories", JsonArray(categories.getCategories().map { c ->
                buildJsonObject {
                    put("id", c.id.toString()); put("name", c.name); put("icon", c.icon)
                    put("path", c.path); put("usePath", c.usePath)
                    put("acceptedFileTypes", JsonArray(c.acceptedFileTypes.map(::JsonPrimitive)))
                    put("acceptedUrlPatterns", JsonArray(c.acceptedUrlPatterns.map(::JsonPrimitive)))
                    put("items", JsonArray(c.items.map { JsonPrimitive(it.toString()) }))
                }
            }))
            put("queues", JsonArray(queues.getAll().map { q ->
                val model = q.getQueueModel()
                buildJsonObject {
                    put("id", q.id.toString()); put("name", model.name); put("active", q.isQueueActive)
                    put("maxConcurrent", model.maxConcurrent); put("stopOnEmpty", model.stopQueueOnEmpty)
                    put("items", JsonArray(model.queueItems.map { JsonPrimitive(it.toString()) }))
                    put("schedule", json.encodeToJsonElement(ScheduleTimes.serializer(), model.scheduledTimes))
                }
            }))
        }
        if (state != lastSnapshot) { revision++; lastSnapshot = state }
        buildJsonObject { state.forEach { (k, v) -> put(k, v) }; put("revision", revision.toString()) }
    }

    suspend fun command(request: JsonObject): JsonObject = withContext(context) {
        val op = request["op"]?.jsonPrimitive?.content ?: error("Missing operation")
        val result = buildJsonObject {
            when (op) {
                "download.probe" -> {
                    // Explicit network read using the existing engine's bounded range probe.
                    // Never constructs a job, persists metadata, reserves an ID or resumes.
                    val url = request["url"]?.jsonPrimitive?.content ?: error("Missing URL")
                    runtime.validateUrl(url)
                    val draft = HttpDownloadItem(link = url, id = -1, folder = destination().path, name = "probe")
                    applyHttpOptions(draft, request)
                    val info = withTimeout(8000) { http.head(draft, 0, 255) }
                    put("statusCode", info.statusCode); put("requiresAuth", info.requiresAuth)
                    put("size", (info.totalLength ?: -1).toString()); put("resumeSupport", info.resumeSupport)
                    put("suggestedName", BrowserLinks.fileName(info.fileName, url))
                    // Do not return captured request/response headers, cookies, redirects,
                    // credentials, ETag or last-modified raw values.
                }
                "download.review" -> {
                    val url = request["url"]?.jsonPrimitive?.content ?: error("Missing URL")
                    runtime.validateUrl(url)
                    val name = BrowserLinks.fileName(request["name"]?.jsonPrimitive?.content, url)
                    val folder = request["folder"]?.jsonPrimitive?.content?.let(Path::of) ?: destination().toPath()
                    runtime.validateFolder(folder)
                    put("name", name); put("existingFile", java.nio.file.Files.exists(folder.resolve(name), java.nio.file.LinkOption.NOFOLLOW_LINKS))
                    put("duplicates", JsonArray(manager.getDownloadList().filter { it.link == url || (it.folder == folder.toString() && it.name == name) }
                        .take(100).map { buildJsonObject { put("id", it.id.toString()); put("name", it.name) } }))
                    put("truncated", manager.getDownloadList().count { it.link == url || (it.folder == folder.toString() && it.name == name) } > 100)
                    put("mutated", false); put("overwriteAllowed", false)
                }
                "add" -> {
                    val url = request["url"]?.jsonPrimitive?.content ?: error("Missing URL")
                    val name = request["name"]?.jsonPrimitive?.content ?: error("Missing file name")
                    val id = addOne(url, name, request["folder"]?.jsonPrimitive?.content?.let(Path::of),
                        request["categoryId"]?.jsonPrimitive?.content ?: "auto", request)
                    if (request["start"]?.jsonPrimitive?.booleanOrNull == true) facade.userManualResume(id)
                    put("id", id.toString())
                }
                "browser.add" -> {
                    // Native-messaging host hand-off: the original AddDownloadsFromIntegration payload.
                    val payload = request["request"]?.jsonObject ?: error("Missing browser request")
                    browserAdd(integrationJson.decodeFromJsonElement(AddDownloadsFromIntegration.serializer(), payload))
                        ?.let { put("requestId", it) }
                }
                "browser.accept" -> {
                    val requestId = request["requestId"]?.jsonPrimitive?.content ?: error("Missing browser request ID")
                    val pending = intake.get(requestId)
                    val chosen = (request["items"] ?: error("Missing browser items")).jsonArray.map { it.jsonObject }
                    require(chosen.isNotEmpty() && chosen.size <= pending.items.size) { "Invalid browser item selection" }
                    // Validate the WHOLE selection against what the frontend saw before adding anything.
                    val selected = chosen.map { choice ->
                        val index = choice["index"]?.jsonPrimitive?.int ?: error("Missing browser item index")
                        val item = pending.items.getOrNull(index) ?: error("Browser item no longer pending")
                        require(choice["url"]?.jsonPrimitive?.content == item.url) { "Browser request changed; review it again" }
                        Triple(index, item, choice["name"]?.jsonPrimitive?.content ?: item.name)
                    }
                    require(selected.map { it.first }.distinct().size == selected.size) { "Duplicate browser item" }
                    require(request["duplicatePolicy"]?.jsonPrimitive?.content != "overwrite" || selected.size == 1) {
                        "Review overwrite one destination at a time; batch replacement is not supported"
                    }
                    val folder = request["folder"]?.jsonPrimitive?.content?.let(Path::of)
                    val selection = request["categoryId"]?.jsonPrimitive?.content ?: "auto"
                    val start = request["start"]?.jsonPrimitive?.booleanOrNull == true
                    val added = mutableListOf<Int>()
                    val ids = mutableListOf<Long>()
                    intake.reserve(requestId)
                    try {
                        for ((index, item, name) in selected) {
                            val id = try { addOne(item.url, name, folder, selection, item.options(request), item.downloadPage) }
                            catch (e: DownloadAddedButCategoryFailed) { added += index; throw e }
                            added += index; ids += id
                            if (start) facade.userManualResume(id)
                        }
                    } catch (e: Exception) {
                        if (added.isEmpty()) throw e
                        throw BrowserAddPartiallyApplied(added.size, selected.size, e)
                    }
                    // Publish completion only after the whole selected operation succeeded.
                    // Failures leave the reservation visible and non-replayable, including
                    // selected items not reached before a partial Add.
                    try { intake.complete(requestId, added) }
                    catch (e: Exception) { throw BrowserAddPartiallyApplied(added.size, selected.size, e) }
                    put("ids", JsonArray(ids.map { JsonPrimitive(it.toString()) }))
                    ids.firstOrNull()?.let { put("id", it.toString()) }
                }
                "browser.dismiss" -> intake.dismiss(request["requestId"]?.jsonPrimitive?.content ?: error("Missing browser request ID"))
                "links.import" -> {
                    // The original "New Download from Clipboard": reviewed like a browser request.
                    val text = request["text"]?.jsonPrimitive?.content ?: error("Missing text")
                    val (items, ignored) = ClipboardLinks.items(text, runtime)
                    require(items.isNotEmpty()) { if (ignored > 0) "The clipboard links are not supported HTTP(S) links" else "No download links found in the clipboard" }
                    put("requestId", intake.offerItems(items, System.currentTimeMillis(), "clipboard").id)
                    put("ignored", ignored)
                }
                "frontend.activate" -> {
                    val token = request["token"]?.takeIf { it !is JsonNull }?.jsonPrimitive?.content
                    require(token == null || (token.length in 1..256 && token.none { it.isWhitespace() || it.isISOControl() })) { "Invalid activation token" }
                    requestActivation(token, "show")
                }
                "settings.integration" -> preferences.updateIntegration(request["settings"]?.jsonObject ?: error("Missing settings"))
                "browser.settings" -> {
                    preferences.updateBrowser(request["settings"]?.jsonObject ?: error("Missing settings"))
                    bridge.apply(preferences.browser())
                }
                "download.edit" -> {
                    val id = request["id"]?.jsonPrimitive?.content?.toLongOrNull() ?: error("Invalid stable ID")
                    val item = list.getById(id) as? HttpDownloadItem ?: error("Download no longer exists")
                    require(item.status != DownloadStatus.Completed && item.status != DownloadStatus.Downloading &&
                        manager.getJobStatusOf(id) !is DownloadJobStatus.IsActive && manager.downloadJobs.none { it.id == id && it.isDownloadActive.value }) { "Pause download before editing" }
                    require(queues.getAll().none { it.isQueueActive && id in it.getQueueModel().queueItems }) { "Stop containing queues before editing" }
                    require(listOf("name", "folder").none { it in request }) { "Moving/renaming payloads is not supported by source/options editing" }
                    val draft = item.copy()
                    request["url"]?.let { draft.link = it.jsonPrimitive.content }
                    runtime.validateUrl(draft.link); runtime.validateFolder(Path.of(draft.folder))
                    applyHttpOptions(draft, request)
                    manual.removeFromQueue(id)
                    // Drains a just-paused active scope before changing the original job's configuration.
                    manager.downloadJobs.find { it.id == id }?.activeDownloadScope?.coroutineContext?.job?.join()
                    try { manager.updateDownloadItem(id, null) { (it as HttpDownloadItem).applyFrom(draft) } }
                    catch (e: CancellationException) { throw e }
                    catch (e: Exception) { throw DownloadEditPartiallyApplied(id, e) }
                    ownedJobs.addAll(manager.downloadJobs)
                    put("id", id.toString())
                }
                "pause", "resume" -> {
                    val id = request["id"]?.jsonPrimitive?.content?.toLongOrNull() ?: error("Invalid stable ID")
                    val item = list.getById(id) ?: error("Download no longer exists")
                    require(item.status != DownloadStatus.Completed) { "Completed download cannot be resumed or paused" }
                    if (op == "pause") facade.manualPause(id) else {
                        runtime.validateUrl(item.link); runtime.validateFolder(Path.of(item.folder))
                        facade.userManualResume(id)
                    }
                    put("id", id.toString())
                }
                "download.forget" -> {
                    val id = request["id"]?.jsonPrimitive?.content?.toLongOrNull() ?: error("Invalid stable ID")
                    require(request["keepFiles"]?.jsonPrimitive?.booleanOrNull == true) { "Only keeping ALL files is supported" }
                    val item = list.getById(id) ?: error("Download no longer exists")
                    require(item.status != DownloadStatus.Downloading && manager.getJobStatusOf(id) !is DownloadJobStatus.IsActive &&
                        manager.downloadJobs.none { it.id == id && it.isDownloadActive.value }) {
                        "Pause download before removing it"
                    }
                    val memberships = queues.getAll().filter { id in it.getQueueModel().queueItems }
                    require(memberships.none { it.isQueueActive }) { "Stop containing queues before removing the download" }
                    // Detach ALL producers synchronously before any suspending persistence.
                    // A scheduled queue starting during I/O can no longer resume this ID.
                    manual.removeFromQueue(id)
                    memberships.forEach { it.removeFromQueue(id) }
                    categories.removeItemInCategories(listOf(id))
                    try {
                        memberships.forEach { it.persistNow() }
                        categories.persistNow()
                        facade.forgetDownloadKeepingFiles(id)
                    } catch (e: CancellationException) { throw e }
                    catch (e: Exception) { throw DownloadForgetPartiallyApplied(id, e) }
                    finally {
                        // Core drains jobs before deleting metadata. Never save a retired
                        // object at shutdown (or overwrite a replacement after a failed delete).
                        ownedJobs.removeAll { it.id == id && it.scope.coroutineContext.job.isCompleted }
                        ownedJobs.addAll(manager.downloadJobs)
                    }
                    put("id", id.toString())
                }
                "settings.update" -> {
                    val patch = request["settings"]?.jsonObject ?: error("Missing settings")
                    // Original defaultDownloadFolder: honored inside the runtime roots only. Empty resets.
                    val rest = JsonObject(patch - "defaultDownloadFolder")
                    val folderValue = patch["defaultDownloadFolder"]
                    if (folderValue != null) {
                        val folder = (folderValue as? JsonPrimitive)?.takeIf { it.isString }?.content ?: error("Invalid default download folder")
                        if (folder.isNotEmpty()) runtime.validateFolder(Path.of(folder))
                        preferences.updateWithDefaultFolder(rest, folder.ifEmpty { null })
                    } else preferences.update(rest)
                    policyController.apply(preferences.policy())
                    limitActive = preferences.speedLimitActive()
                    updateActivityMonitor()
                }
                "queue.create" -> {
                    val name = queueName(request)
                    require(queues.getAll().size < 128) { "Queue limit reached" }
                    queues.addQueue(name)
                    put("queueId", queues.getAll().last().id.toString())
                }
                "queue.update", "queue.schedule", "queue.add", "queue.remove", "queue.move", "queue.start", "queue.stop", "queue.delete" -> {
                    val queueId = request["queueId"]?.jsonPrimitive?.content?.toLongOrNull() ?: error("Invalid queue ID")
                    val q = queues.getAll().find { it.id == queueId } ?: error("Queue no longer exists")
                    when (op) {
                        "queue.update" -> {
                            // Validate the complete patch BEFORE mutating any property.
                            val name = queueName(request)
                            val concurrent = request["maxConcurrent"]?.jsonPrimitive?.int ?: error("Missing concurrency")
                            require(concurrent in 1..1024) { "Queue concurrency must be 1–1024" }
                            val stop = request["stopOnEmpty"]?.jsonPrimitive?.boolean ?: error("Missing stop policy")
                            q.setName(name); q.setMaxConcurrent(concurrent); q.setStopQueueOnEmpty(stop)
                        }
                        "queue.schedule" -> {
                            val schedule = request["schedule"]?.jsonObject ?: error("Missing schedule")
                            // Strict serializer validates weekday/time/boolean fields and rejects unknown keys.
                            val parsed = json.decodeFromJsonElement(ScheduleTimes.serializer(), schedule)
                            q.setScheduledTimes { parsed }
                        }
                        "queue.start" -> q.start()
                        "queue.stop" -> q.stopAsync()
                        "queue.delete" -> {
                            require(queues.canDelete(queueId)) { "Main queue cannot be deleted" }
                            require(!q.isQueueActive) { "Stop queue before deleting it" }
                            try { facade.deleteQueue(queueId) }
                            catch (e: CancellationException) { throw e }
                            catch (e: Exception) { throw QueueDeletePartiallyApplied(queueId, e) }
                        }
                        else -> {
                            val id = request["id"]?.jsonPrimitive?.content?.toLongOrNull() ?: error("Invalid download ID")
                            val item = list.getById(id) ?: error("Download no longer exists")
                            when (op) {
                                "queue.add" -> {
                                    require(item.status != DownloadStatus.Completed) { "Completed download cannot be queued" }
                                    require(manager.getJobStatusOf(id) !is DownloadJobStatus.IsActive) { "Pause download before moving it into a queue" }
                                    manual.removeFromQueue(id); queues.addToQueue(queueId, id)
                                }
                                "queue.remove" -> {
                                    require(manager.getJobStatusOf(id) !is DownloadJobStatus.IsActive) { "Pause download before removing it from a queue" }
                                    q.removeFromQueue(id)
                                }
                                "queue.move" -> {
                                    val direction = request["direction"]?.jsonPrimitive?.int ?: error("Missing direction")
                                    require(direction == -1 || direction == 1) { "Direction must be -1 or 1" }
                                    q.move(listOf(id), direction)
                                }
                            }
                        }
                    }
                    // Commands acknowledge persisted configuration/items, not merely an autosave enqueue.
                    if (op != "queue.delete") q.persistNow()
                    put("queueId", queueId.toString())
                }
                "category.create", "category.update", "category.delete", "category.move", "category.assign" -> {
                    val before = categories.getCategories()
                    try {
                        if (op == "category.create") {
                            require(before.size < 128) { "Category limit reached" }
                            val category = categoryDraft(request, Category(-1, "", "icon:videoFile", destination().path, false))
                            categories.addCustomCategory(category)
                            put("categoryId", categories.getCategories().last().id.toString())
                        } else if (op == "category.assign") {
                            val id = request["id"]?.jsonPrimitive?.content?.toLongOrNull() ?: error("Invalid download ID")
                            require(list.getById(id) != null) { "Download no longer exists" }
                            val selected = request["categoryId"]?.jsonPrimitive?.content ?: error("Missing category ID")
                            if (selected == "none") categories.removeItemInCategories(listOf(id))
                            else {
                                val c = categories.getCategoryById(selected.toLongOrNull() ?: error("Invalid category ID"))
                                    ?: error("Category no longer exists")
                                categories.addItemsToCategory(c.id, listOf(id))
                            }
                        } else {
                            val id = request["categoryId"]?.jsonPrimitive?.content?.toLongOrNull() ?: error("Invalid category ID")
                            val category = categories.getCategoryById(id) ?: error("Category no longer exists")
                            when (op) {
                                "category.update" -> categories.updateCategory(categoryDraft(request, category))
                                "category.delete" -> categories.deleteCategory(id) // Records/files are never removed.
                                "category.move" -> {
                                    val direction = request["direction"]?.jsonPrimitive?.int ?: error("Missing direction")
                                    require(direction == -1 || direction == 1) { "Direction must be -1 or 1" }
                                    categories.reorderCategory(before.indexOf(category), direction)
                                }
                            }
                        }
                        categories.persistNow()
                    } catch (e: Exception) {
                        categories.setCategories(before)
                        throw e
                    }
                }
                else -> error("Unsupported command")
            }
            put("ok", true)
        }
        result
    }

    private fun categoryDraft(request: JsonObject, previous: Category): Category {
        require(listOf("path", "usePath", "icon", "items").none { it in request }) {
            "Category path/icon/membership fields must use supported isolated workflows"
        }
        fun filters(key: String): List<String> = (request[key] ?: error("Missing $key")).jsonArray.map {
            require(it is JsonPrimitive && it.isString) { "Invalid filter" }; it.content
        }
        val name = request["name"]?.jsonPrimitive?.content?.trim() ?: error("Missing category name")
        val draft = previous.copy(name = name, acceptedFileTypes = filters("acceptedFileTypes"),
            acceptedUrlPatterns = filters("acceptedUrlPatterns"))
        validateCategories(listOf(draft), allowDraft = previous.id == -1L)
        return draft
    }

    private fun validateCategories(values: List<Category>, allowDraft: Boolean = false) {
        require(values.size <= 128 && values.map { it.id }.distinct().size == values.size) { "Invalid categories" }
        values.forEach { c ->
            require(c.name.length in 1..120 && c.name.none { it.isISOControl() }) { "Invalid category name" }
            require((c.id >= 0 || (allowDraft && c.id == -1L)) && c.id < Long.MAX_VALUE - 1 && c.icon.length <= 4096) { "Invalid category ID/icon" }
            require(File(c.path).canonicalFile == downloads.canonicalFile && !c.usePath) {
                "Category destinations are unavailable in isolated mode"
            }
            require(c.items.size == c.items.distinct().size) { "Duplicate category membership" }
            require(c.acceptedFileTypes.size <= 128 && c.acceptedFileTypes.all {
                it.length in 1..40 && it.none { ch -> ch.isWhitespace() || ch.isISOControl() || ch == '/' || ch == '\\' }
            }) { "Invalid category file types" }
            require(c.acceptedUrlPatterns.size <= 32 && c.acceptedUrlPatterns.all {
                it.length in 1..512 && it.none { ch -> ch.isISOControl() }
            }) { "Invalid category URL patterns" }
        }
    }

    private fun queueName(request: JsonObject): String {
        val name = request["name"]?.jsonPrimitive?.content?.trim() ?: error("Missing queue name")
        require(name.length in 1..120 && name.none { it.isISOControl() }) { "Invalid queue name" }
        return name
    }

    suspend fun stop() {
        // Outside the owner thread: in-flight browser requests may be waiting for it.
        bridge.close()
        notifier.onActivated = null
        activity.close()
        withContext(context) {
            // Disable/cancel and join EVERY queue producer before pausing transfer jobs.
            // Final queue persistence runs after autosavers have joined, with lease still held.
            facade.closeListenersAndJoin()
            monitorSubscription?.cancelAndJoin(); monitorSubscription = null
            monitor.closeAndJoin()
            queues.closeAndJoin()
            categories.closeAndJoin()
            queueScope.coroutineContext.job.cancelAndJoin()
            // Include Resuming/PreparingFile jobs, not just Downloading as stopAll currently does.
            // Keep references even after core removes completed jobs. Cancellation alone is
            // insufficient: join every writer before saving finally and releasing the lease.
            ownedJobs.addAll(manager.downloadJobs)
            ownedJobs.filter { it.downloadItem.status != DownloadStatus.Completed }.forEach { it.pause() }
            ownedJobs.forEach {
                it.activeDownloadScope?.coroutineContext?.job?.cancelAndJoin()
                it.scope.coroutineContext.job.cancelAndJoin()
            }
            manager.scope.coroutineContext.job.cancelAndJoin()
            ownedJobs.forEach { if (list.getById(it.id) != null) it.saveState() }
            // A completion event can race listener cancellation. Reconcile only after all
            // engine writers joined; never leave completed IDs stranded in persisted queues.
            val completedIds = list.getAll().filter { it.status == DownloadStatus.Completed }.map { it.id }
            queues.getAll().forEach { it.removeFromQueue(completedIds); it.persistNow() }
            ownedJobs.clear()
            client.dispatcher.executorService.shutdown()
            client.connectionPool.evictAll()
        }
        context.close()
        notifier.close()
    }
}

class QueueDeletePartiallyApplied(val queueId: Long, cause: Exception) : IllegalStateException(
    "Queue deletion may be partially applied, including extra settings. Inspect refreshed queues before explicitly retrying; no automatic retry was requested.", cause)

class DownloadEditPartiallyApplied(val downloadId: Long, cause: Exception) : IllegalStateException(
    "Source/options edit may be partially applied. Inspect refreshed details before explicitly retrying; no automatic resume was requested.", cause)

class DownloadForgetPartiallyApplied(val downloadId: Long, cause: Exception) : IllegalStateException(
    "Removal may be partially applied. All payload files were kept. Inspect the refreshed list and queue/category membership before retrying.", cause)

class BrowserAddPartiallyApplied(val added: Int, val total: Int, cause: Exception) : IllegalStateException(
    "$added of $total browser links were added before an error. Inspect the refreshed list; the rest were not added and are not retried automatically.", cause)

class DownloadReplacePartiallyApplied(val downloadId: Long, cause: Exception) : IllegalStateException(
    "Replacement may be partially applied: the previous download or its files may be removed and the replacement may be incomplete. Inspect the refreshed list before explicitly retrying; nothing is retried automatically.", cause)

class DownloadAddedButCategoryFailed(val downloadId: Long, cause: Exception) : IllegalStateException(
    "Download was added but category persistence failed. Inspect the refreshed list before retrying.", cause)

/** Acquires the common profile lock before invoking the persistence factory; releases
 * only after all jobs have stopped/saved. A failed stop must not admit another writer.
 */
class ServiceLifecycle(
    private val profile: Path, private val runtime: RuntimePolicy = RuntimePolicy.isolated(profile),
    private val browserIntegration: Boolean = !runtime.isolated, private val launcher: FrontendLauncher = FrontendLauncher(null),
    private val notifier: () -> DesktopNotifier = { NoDesktopNotifier }, private val activityCommand: List<String>? = null,
) {
    private var lease: ProfileLease? = null
    private var engine: EngineSession? = null
    suspend fun start(): EngineSession {
        check(lease == null)
        ProfileCopies.requireNotQuarantined(profile)
        lease = ProfileLease.acquire(profile.resolve("app-data/config/app.lock"))
        var desktop: DesktopNotifier? = null
        try {
            desktop = notifier()
            return EngineSession(profile, runtime, browserIntegration, launcher, desktop, activityCommand).also { engine = it; it.boot() }
        } catch (e: Throwable) {
            try {
                if (engine == null) desktop?.close() else engine?.stop()
                engine = null; lease?.close(); lease = null
            } catch (stop: Throwable) { e.addSuppressed(stop) }
            throw e
        }
    }
    suspend fun stop() { engine?.stop(); engine = null; lease?.close(); lease = null }
}
