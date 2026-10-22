package com.abdownloadmanager.service

import ir.amirab.downloader.downloaditem.http.HttpDownloadItem
import kotlinx.serialization.json.*

/** Browser cookies routinely exceed 8 KiB. Boot re-validates stored items with these same
 * bounds, so they may only grow: a narrower bound would refuse an existing profile.
 */
object HttpOptionBounds {
    const val HEADER_VALUE = 32768
    const val HEADERS_TOTAL = 65536
}

/** Original HttpDownloadItem fields/serializer, not an auxiliary credentials database.
 * Secrets are write-only over IPC; normal snapshots never return them.
 */
internal fun applyHttpOptions(item: HttpDownloadItem, request: JsonObject) {
    request["connections"]?.let {
        item.preferredConnectionCount = if (it is JsonNull) null else it.jsonPrimitive.int.also { count -> require(count in 1..256) { "Connections must be 1–256" } }
    }
    request["speedLimit"]?.let {
        require(it is JsonPrimitive && it.isString) { "Speed limit must be a string" }
        item.speedLimit = it.content.toLong().also { speed -> require(speed >= 0) { "Invalid per-download speed limit" } }
    }
    request["credentials"]?.let {
        val credentials = it.jsonObject
        require(credentials.keys.all { key -> key in setOf("username", "password", "userAgent", "headers") }) { "Unsupported credential field" }
        fun text(key: String, bound: Int): String? = credentials[key]?.let { value ->
            require(value is JsonPrimitive && value.isString && value.content.length <= bound && value.content.none(Char::isISOControl)) { "Invalid credential text" }
            value.content.takeIf(String::isNotEmpty)
        }
        val username = text("username", 4096)
        val password = text("password", 4096)
        require((username == null) == (password == null)) { "Basic authentication requires both username and password" }
        val userAgent = text("userAgent", 1024)
        val headers = credentials["headers"]?.jsonObject?.mapValues { (_, value) ->
            require(value is JsonPrimitive && value.isString && value.content.length <= HttpOptionBounds.HEADER_VALUE && value.content.none(Char::isISOControl)) { "Invalid header value" }
            value.content
        }.orEmpty()
        require(headers.size <= 64 && headers.keys.map(String::lowercase).distinct().size == headers.size && headers.keys.all { key ->
            key.matches(Regex("[!#$%&'*+.^_`|~0-9A-Za-z-]{1,128}")) && key.lowercase() !in setOf("host", "range", "content-length", "connection", "transfer-encoding", "proxy-authorization", "proxy-connection")
        }) { "Invalid or engine-owned HTTP header" }
        require(headers.entries.sumOf { (key, value) -> key.length + value.length } <= HttpOptionBounds.HEADERS_TOTAL) { "HTTP headers too large" }
        item.username = username; item.password = password; item.userAgent = userAgent; item.headers = headers.takeIf { it.isNotEmpty() }
    }
}

internal fun validateHttpOptions(item: HttpDownloadItem) {
    val request = buildJsonObject {
        put("connections", item.preferredConnectionCount?.let(::JsonPrimitive) ?: JsonNull)
        put("speedLimit", item.speedLimit.toString())
        put("credentials", buildJsonObject {
            put("username", item.username.orEmpty()); put("password", item.password.orEmpty()); put("userAgent", item.userAgent.orEmpty())
            put("headers", buildJsonObject { item.headers.orEmpty().forEach { (key, value) -> put(key, value) } })
        })
    }
    applyHttpOptions(item.copy(), request)
}
