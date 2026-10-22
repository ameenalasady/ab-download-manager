package com.abdownloadmanager.service

import com.abdownloadmanager.integration.model.AddDownloadsFromIntegration
import com.abdownloadmanager.integration.model.HLSDownloadCredentialsFromIntegration
import com.abdownloadmanager.integration.model.HttpDownloadCredentialsFromIntegration
import com.abdownloadmanager.integration.model.IDownloadCredentialsFromIntegration
import kotlinx.serialization.json.*
import java.net.URLDecoder
import java.security.SecureRandom
import java.util.Base64

/** One browser-supplied link, validated and reduced to what the HTTP engine stores.
 * [credentials] uses the IPC `credentials` shape of [applyHttpOptions] and stays in the
 * service: snapshots report only [hasHeaders], never cookies or other captured headers.
 */
class BrowserItem(val url: String, val name: String, val downloadPage: String?, val credentials: JsonObject?) {
    val hasHeaders get() = credentials != null
}

/** Review source: browser extension or explicit text intake (clipboard/Multiple Add). */
class BrowserRequest(val id: String, val items: List<BrowserItem>, val receivedAt: Long, val source: String = "browser", val outcomeUnknown: Boolean = false)

object BrowserLinks {
    // Engine-owned, hop-by-hop or browser-internal: never replayed by the downloader.
    private val droppedHeaders = setOf("host", "range", "content-length", "connection", "transfer-encoding",
        "proxy-authorization", "proxy-connection", "keep-alive", "upgrade", "te", "trailer", "expect",
        "accept-encoding", "if-range", "if-match", "if-none-match", "if-modified-since", "if-unmodified-since")
    private val token = Regex("[!#$%&'*+.^_`|~0-9A-Za-z-]{1,128}")

    /** Converts captured browser headers to the engine's credential fields. Invalid or
     * engine-owned headers are dropped (the original forwards them verbatim to OkHttp,
     * which rejects or overrides several of them). User-Agent becomes the item agent.
     */
    fun credentials(headers: Map<String, String>?): JsonObject? {
        if (headers.isNullOrEmpty()) return null
        var userAgent: String? = null
        val kept = linkedMapOf<String, String>()
        for ((key, value) in headers) {
            val lower = key.lowercase()
            if (!key.matches(token) || lower in droppedHeaders || lower.startsWith("sec-") ||
                value.length > HttpOptionBounds.HEADER_VALUE || value.any(Char::isISOControl)) continue
            if (lower == "user-agent") { if (value.length <= 1024) userAgent = value; continue }
            if (kept.keys.none { it.equals(key, ignoreCase = true) }) kept[key] = value
        }
        require(kept.size <= 64 && kept.entries.sumOf { (k, v) -> k.length + v.length } <= HttpOptionBounds.HEADERS_TOTAL) {
            "Browser request headers are too large"
        }
        if (kept.isEmpty() && userAgent == null) return null
        return buildJsonObject {
            userAgent?.let { put("userAgent", it) }
            put("headers", buildJsonObject { kept.forEach { (k, v) -> put(k, v) } })
        }
    }

    /** Original fallback: suggested name, else the last URL path segment. Made safe for
     * the service's file-name rules instead of rejecting the whole browser request.
     */
    fun fileName(suggested: String?, url: String): String {
        fun clean(value: String?) = value
            ?.map { if (it == '/' || it == '\\' || it.isISOControl()) '_' else it }?.joinToString("")
            ?.trim()?.trim('.')?.takeIf { it.isNotEmpty() }
            ?.let { if (it.length <= 180) it else it.substringAfterLast('.', "").let { ext ->
                if (ext.length in 1..16) it.take(179 - ext.length) + "." + ext else it.take(180) } }
        val segment = runCatching {
            URLDecoder.decode(java.net.URI(url).rawPath.orEmpty().substringAfterLast('/'), Charsets.UTF_8)
        }.getOrNull()
        return clean(suggested) ?: clean(segment) ?: "download"
    }

    fun item(source: IDownloadCredentialsFromIntegration, runtime: RuntimePolicy): BrowserItem {
        require(source is HttpDownloadCredentialsFromIntegration) {
            if (source is HLSDownloadCredentialsFromIntegration) "HLS downloads are not supported by this service yet" else "Unsupported download type"
        }
        runtime.validateUrl(source.link)
        val page = source.downloadPage?.takeIf { it.length <= 2048 && it.none(Char::isISOControl) }
        return BrowserItem(source.link, fileName(source.suggestedName, source.link), page, credentials(source.headers))
    }
}

/** Bounded review-only requests. Journal publication precedes acknowledgement and Add.
 * Reservations survive restart as unknown outcomes; they cannot be accepted again.
 * Owned by the service command thread, under the common profile lease.
 */
class BrowserIntake(private val maxItems: Int = 100, journalFile: java.nio.file.Path? = null) {
    private val random = SecureRandom()
    private val journal = journalFile?.let(::BrowserRequestJournal)
    private val pending = linkedMapOf<String, BrowserRequest>()
    private var healthy = true
    val requests: List<BrowserRequest> get() = pending.values.toList()
    init {
        journal?.load()?.forEach { r ->
            require(r.id.matches(Regex("[A-Za-z0-9_-]{16}")) && r.source in setOf("browser", "clipboard") && r.receivedAt >= 0 && r.items.isNotEmpty())
            require(pending.put(r.id, r) == null) { "Duplicate browser review ID" }
            r.items.forEach { i ->
                require(i.url.length in 1..2048 && i.name.length in 1..180 && i.name !in setOf(".", "..") &&
                    i.name.none { it == '/' || it == '\\' || it.isISOControl() } &&
                    (i.downloadPage == null || (i.downloadPage.length <= 2048 && i.downloadPage.none(Char::isISOControl))))
                // Validate credentials using the actual Add validator, without making a job.
                val draft = ir.amirab.downloader.downloaditem.http.HttpDownloadItem(link = i.url, id = -1, folder = "/", name = i.name)
                i.credentials?.let { applyHttpOptions(draft, buildJsonObject { put("credentials", it) }) }
            }
        }
        require(pending.size <= maxItems && pending.values.sumOf { it.items.size } <= maxItems) { "Oversized browser review journal" }
    }
    private fun publish(next: Map<String, BrowserRequest>) {
        check(healthy) { "Browser journal outcome unknown; restart and inspect retained evidence" }
        try { journal?.save(next.values) }
        catch (e: Exception) { healthy = false; throw IllegalStateException("Browser journal publication failed; no automatic retry. Inspect retained evidence.", e) }
        pending.clear(); pending.putAll(next)
    }

    fun offer(request: AddDownloadsFromIntegration, runtime: RuntimePolicy, now: Long): BrowserRequest {
        require(request.items.isNotEmpty()) { "Browser request contains no links" }
        return offerItems(request.items.map { BrowserLinks.item(it, runtime) }, now, "browser")
    }

    fun offerItems(items: List<BrowserItem>, now: Long, source: String): BrowserRequest {
        require(items.isNotEmpty()) { "No downloadable links found" }
        require(pending.values.sumOf { it.items.size } + items.size <= maxItems) {
            "Too many links are waiting. Review or dismiss pending requests in AB Download Manager."
        }
        val id = Base64.getUrlEncoder().withoutPadding().encodeToString(ByteArray(12).also(random::nextBytes))
        require(source in setOf("browser", "clipboard"))
        return BrowserRequest(id, items, now, source).also { publish(pending + (id to it)) }
    }

    fun get(id: String): BrowserRequest {
        check(healthy) { "Browser journal needs explicit inspection" }
        val request = pending[id] ?: error("Browser request is no longer pending")
        require(!request.outcomeUnknown) { "Previous acceptance outcome is unknown. Inspect downloads; only explicit discard is allowed, never replay." }
        return request
    }

    /** Persist BEFORE the first mutation; crashes/partial outcomes cannot replay acceptance. */
    fun reserve(id: String) {
        val r = get(id)
        publish(pending + (id to BrowserRequest(r.id, r.items, r.receivedAt, r.source, true)))
    }

    /** Removes accepted items; the request disappears once nothing is left. */
    fun complete(id: String, indices: Collection<Int>) {
        val request = pending[id] ?: return
        val left = request.items.filterIndexed { index, _ -> index !in indices }
        val next = pending.toMutableMap()
        if (left.isEmpty()) next.remove(id) else next[id] = BrowserRequest(id, left, request.receivedAt, request.source)
        publish(next)
    }

    fun dismiss(id: String) {
        require(id in pending) { "Browser request is no longer pending" }
        publish(pending - id)
    }

    /** [probe]: optional per-item Check Sizes state, added as `probe`. */
    fun snapshot(probe: (BrowserItem) -> JsonObject? = { null }) = JsonArray(pending.values.map { request ->
        buildJsonObject {
            put("id", request.id); put("receivedAt", request.receivedAt.toString()); put("source", request.source)
            put("outcomeUnknown", request.outcomeUnknown || !healthy)
            put("items", JsonArray(request.items.map { item ->
                buildJsonObject {
                    put("url", item.url); put("name", item.name); put("hasHeaders", item.hasHeaders)
                    put("downloadPage", item.downloadPage?.let(::JsonPrimitive) ?: JsonNull)
                    probe(item)?.let { put("probe", it) }
                }
            }))
        }
    })
}
