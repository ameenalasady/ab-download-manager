package com.abdownloadmanager.service

import kotlinx.serialization.json.*
import org.freedesktop.dbus.annotations.DBusInterfaceName
import org.freedesktop.dbus.connections.impl.DBusConnection
import org.freedesktop.dbus.connections.impl.DBusConnectionBuilder
import org.freedesktop.dbus.interfaces.DBusInterface
import org.freedesktop.dbus.interfaces.DBusSigHandler
import org.freedesktop.dbus.messages.DBusSignal
import org.freedesktop.dbus.types.UInt32
import org.freedesktop.dbus.types.Variant
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

/** One desktop notification. [action] is what clicking it should do in the frontend
 * ("show" or "review"); [downloadId] selects that download when the window opens.
 */
data class DesktopNotice(val key: String, val title: String, val body: String, val error: Boolean,
                         val sound: Boolean, val action: String, val downloadId: Long? = null)

/** Native equivalent of the original's in-app notification popup plus beep. */
interface DesktopNotifier : AutoCloseable {
    val available: Boolean
    fun notify(notice: DesktopNotice)
    /** Called (on any thread) when the user clicks a notification; token may be null. */
    var onActivated: ((notice: DesktopNotice, token: String?) -> Unit)?
    override fun close() {}
}

object NoDesktopNotifier : DesktopNotifier {
    override val available = false
    override fun notify(notice: DesktopNotice) {}
    override var onActivated: ((DesktopNotice, String?) -> Unit)? = null
}

@DBusInterfaceName("org.freedesktop.Notifications")
interface FreedesktopNotifications : DBusInterface {
    @JvmSuppressWildcards
    fun Notify(appName: String, replacesId: UInt32, appIcon: String, summary: String, body: String,
               actions: List<String>, hints: Map<String, Variant<*>>, expireTimeout: Int): UInt32

    class ActionInvoked(path: String, val id: UInt32, val actionKey: String) : DBusSignal(path, id, actionKey)
    /** Notifications spec 1.2: emitted right before ActionInvoked, for xdg-activation. */
    class ActivationToken(path: String, val id: UInt32, val token: String) : DBusSignal(path, id, token)
}

/** org.freedesktop.Notifications over the session bus. D-Bus calls run on one daemon
 * thread, never on the engine owner thread. The summary is never markup by spec; the body
 * may be (Plasma advertises body-markup), so its '&', '<' and '>' are escaped.
 * Unavailable buses or servers are silently treated as "no notifications".
 */
class FreedesktopNotifier(
    private val connect: () -> DBusConnection = { DBusConnectionBuilder.forSessionBus().withShared(false).build() },
    private val desktopEntry: String = "com.abdownloadmanager",
) : DesktopNotifier {
    private val worker = Executors.newSingleThreadScheduledExecutor { r -> Thread(r, "desktop-notifier").apply { isDaemon = true } }
    private var connection: DBusConnection? = null
    private var server: FreedesktopNotifications? = null
    private val sent = ConcurrentHashMap<Long, DesktopNotice>()
    private val tokens = ConcurrentHashMap<Long, String>()
    private val replaced = ConcurrentHashMap<String, UInt32>()
    @Volatile override var available = false; private set
    @Volatile override var onActivated: ((DesktopNotice, String?) -> Unit)? = null

    init { worker.execute { establish() } }

    private fun establish() {
        if (server != null) return
        runCatching {
            val c = connect()
            connection = c // Close even if proxy/handler initialization fails.
            val target = c.getRemoteObject("org.freedesktop.Notifications", "/org/freedesktop/Notifications", FreedesktopNotifications::class.java)
            val owner = c.getDBusOwnerName("org.freedesktop.Notifications")
            c.addSigHandler(FreedesktopNotifications.ActivationToken::class.java, owner, target, DBusSigHandler { s ->
                if (sent.containsKey(s.id.toLong()) && s.token.length in 1..256 && s.token.none { it.isWhitespace() || it.isISOControl() })
                    tokens[s.id.toLong()] = s.token
            })
            c.addSigHandler(FreedesktopNotifications.ActionInvoked::class.java, owner, target, DBusSigHandler { s ->
                if (s.actionKey == "default" && !worker.isShutdown) {
                    // D-Bus transports may dispatch consecutive signals on different threads.
                    worker.schedule({ sent[s.id.toLong()]?.let { onActivated?.invoke(it, tokens.remove(s.id.toLong())) } }, 50, TimeUnit.MILLISECONDS)
                }
            })
            server = target; available = true
        }.onFailure { reset() }
    }

    private fun reset() {
        available = false; server = null
        runCatching { connection?.close() }; connection = null
        sent.clear(); tokens.clear(); replaced.clear()
    }

    private fun plain(text: String) = text.take(512).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")

    override fun notify(notice: DesktopNotice) {
        if (worker.isShutdown) return
        worker.execute {
            establish()
            val target = server ?: return@execute
            runCatching {
                val hints = linkedMapOf<String, Variant<*>>(
                    "desktop-entry" to Variant(desktopEntry),
                    "urgency" to Variant(if (notice.error) 2.toByte() else 1.toByte()),
                )
                // Freedesktop sound-naming spec names, the closest match to the original beeps.
                if (notice.sound) hints["sound-name"] = Variant(if (notice.error) "dialog-error" else "complete-download")
                else hints["suppress-sound"] = Variant(true)
                val id = target.Notify("AB Download Manager", replaced[notice.key] ?: UInt32(0), "",
                    notice.title.take(512), plain(notice.body), listOf("default", "Open"), hints, -1)
                replaced[notice.key] = id
                sent[id.toLong()] = notice
                if (sent.size > 256) sent.keys.sorted().take(sent.size - 256).forEach { old ->
                    sent.remove(old); tokens.remove(old); replaced.entries.removeIf { it.value.toLong() == old }
                }
            }.onFailure { reset() }
        }
    }

    @Synchronized override fun close() {
        if (worker.isShutdown) return
        worker.execute { reset() }
        worker.shutdown()
        check(worker.awaitTermination(10, TimeUnit.SECONDS)) { "Desktop notifier did not drain" }
    }
}

/** Port of the original DefaultDownloadCredentialsExtractor: JSON credentials, then curl
 * commands, then plain links (one per line, else found anywhere in the text). The first
 * extractor that yields links wins; duplicates by link are dropped. Only HTTP(S) links the
 * runtime policy accepts are kept; others are counted as ignored, never guessed at.
 */
object ClipboardLinks {
    const val MAX_TEXT = 256 * 1024
    private val urlRegex = Regex("""\b(https?|ftp|file)://[-a-zA-Z0-9+&@#/%?=~_|!:,.;]*[-a-zA-Z0-9+&@#/%=~_|]""")

    data class Candidate(val link: String, val headers: Map<String, String> = emptyMap(),
                         val username: String? = null, val password: String? = null,
                         val downloadPage: String? = null, val suggestedName: String? = null)

    fun candidates(text: String): List<Candidate> {
        require(text.length <= MAX_TEXT) { "Clipboard text is too large" }
        val extractors = listOf(::fromJson, ::fromCurl, ::fromLinks)
        return extractors.firstNotNullOfOrNull { e -> runCatching { e(text).takeIf { it.isNotEmpty() } }.getOrNull() }
            ?.distinctBy { it.link } ?: emptyList()
    }

    private fun fromJson(text: String): List<Candidate> {
        val element = Json.parseToJsonElement(text)
        val objects = if (element is JsonArray) element.map { it.jsonObject } else listOf(element.jsonObject)
        return objects.map { o ->
            fun text(key: String) = o[key]?.takeIf { it !is JsonNull }?.jsonPrimitive?.content
            Candidate(text("link") ?: error("Missing link"),
                o["headers"]?.takeIf { it !is JsonNull }?.jsonObject?.mapValues { it.value.jsonPrimitive.content }.orEmpty(),
                text("username"), text("password"), text("downloadPage"), text("suggestedName") ?: text("name"))
        }
    }

    /** Tokenization only, never a shell: quotes/line continuation in browser Copy as cURL.
     * No substitution, expansions, redirects or commands are evaluated. */
    private fun curlTokens(command: String): List<String> {
        val tokens = mutableListOf<String>(); val token = StringBuilder()
        var quote: Char? = null; var escaped = false; var started = false
        for (ch in command) {
            when {
                escaped -> { if (ch != '\n') token.append(ch); escaped = false }
                ch == '\\' && quote != '\'' -> { escaped = true; started = true }
                quote != null -> { if (ch == quote) quote = null else token.append(ch) }
                ch == '\'' || ch == '"' -> { quote = ch; started = true }
                ch.isWhitespace() -> if (started) { tokens += token.toString(); token.clear(); started = false }
                else -> { token.append(ch); started = true }
            }
        }
        require(quote == null && !escaped) { "Unfinished curl quoting" }
        if (started) tokens += token.toString()
        return tokens
    }

    private fun fromCurl(text: String): List<Candidate> = text.replace("\\\n", " ")
        .split(Regex("(?m)(?=^\\s*curl\\s)")) .filter { it.trim().startsWith("curl ") }.mapNotNull { command ->
            val tokens = curlTokens(command.trim())
            var link: String? = null; var username: String? = null; var password: String? = null
            val headers = linkedMapOf<String, String>(); var i = 1
            while (i < tokens.size) {
                val option = tokens[i++]
                fun argument() = tokens.getOrNull(i++) ?: error("Missing curl argument")
                when (option) {
                    "-H", "--header" -> {
                        val parts = argument().split(":", limit = 2)
                        if (parts.size == 2) headers[parts[0].trim()] = parts[1].trim()
                    }
                    "-A", "--user-agent" -> headers["User-Agent"] = argument()
                    "-b", "--cookie" -> headers["Cookie"] = argument()
                    "-u", "--user" -> {
                        val parts = argument().split(":", limit = 2)
                        require(parts.size == 2) { "Missing curl password" }
                        username = parts[0]; password = parts[1]
                    }
                    "--url" -> link = argument()
                    "-d", "--data", "--data-raw", "--data-binary", "-X", "--request", "-o", "--output", "--referer", "-e" -> argument()
                    else -> if (option.startsWith("--url=")) link = option.substringAfter('=')
                        else if (link == null && option.startsWith("http")) link = option
                }
            }
            link?.let { Candidate(it, headers, username, password) }
        }

    private fun fromLinks(text: String): List<Candidate> {
        val lines = text.lineSequence().map { it.trim() }.filter { valid(it) }.toList()
        return (lines.ifEmpty { urlRegex.findAll(text).map { it.value }.filter { valid(it) }.toList() }).map { Candidate(it) }
    }

    private fun valid(value: String) = runCatching {
        val uri = java.net.URI(value)
        uri.scheme in listOf("http", "https") && !uri.host.isNullOrBlank()
    }.getOrDefault(false)

    /** Validated items for the review dialog, plus how many links were ignored. */
    fun items(text: String, runtime: RuntimePolicy): Pair<List<BrowserItem>, Int> {
        val found = candidates(text)
        val items = found.mapNotNull { c ->
            runCatching {
                runtime.validateUrl(c.link)
                val captured = BrowserLinks.credentials(c.headers)
                require((c.username == null) == (c.password == null)) { "Incomplete clipboard credentials" }
                require(listOfNotNull(c.username, c.password).all { it.length <= 4096 && it.none(Char::isISOControl) }) { "Invalid clipboard credentials" }
                val basic = c.username != null && c.password != null
                val credentials = if (captured == null && !basic) null else buildJsonObject {
                    captured?.forEach { (k, v) -> put(k, v) }
                    if (captured == null) put("headers", buildJsonObject {})
                    if (basic) { put("username", c.username); put("password", c.password) }
                }
                val page = c.downloadPage?.takeIf { it.length <= 2048 && it.none(Char::isISOControl) }
                BrowserItem(c.link, BrowserLinks.fileName(c.suggestedName, c.link), page, credentials)
            }.getOrNull()
        }
        return items to (found.size - items.size)
    }
}
