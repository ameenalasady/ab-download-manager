package com.abdownloadmanager.integration

import io.ktor.http.HttpHeaders
import io.ktor.http.HttpStatusCode
import io.ktor.server.application.createApplicationPlugin
import io.ktor.server.application.install
import io.ktor.server.cio.CIO
import io.ktor.server.engine.embeddedServer
import io.ktor.server.response.respondText
import kotlinx.serialization.json.Json

/**
 * Synchronous variant of [Integration] for hosts without a reactive settings flow.
 * Same endpoints, loopback bind and X-Api-Key authentication as the original server.
 *
 * Additionally refuses requests a web page can forge: a `Host` other than this loopback
 * port (DNS rebinding) and an `Origin` of a web/file/opaque document (cross-site form
 * posts). Browser-extension origins and origin-less local clients are unaffected.
 */
fun createIntegrationServer(
    json: Json,
    integrationHandler: IntegrationHandler,
    settings: IntegrationSettings,
): MyServer {
    val allowedHosts = setOf("127.0.0.1:${settings.port}", "localhost:${settings.port}")
    val server = embeddedServer(
        factory = CIO,
        port = settings.port,
        host = "127.0.0.1",
    ) {
        install(createApplicationPlugin("LoopbackRequestGuard") {
            onCall { call ->
                val host = call.request.headers[HttpHeaders.Host]?.lowercase()
                val origin = call.request.headers[HttpHeaders.Origin]?.lowercase()
                val webOrigin = origin != null && (origin == "null" ||
                    listOf("http:", "https:", "file:", "data:", "blob:").any { origin.startsWith(it) })
                if (host !in allowedHosts || webOrigin) {
                    call.respondText("Forbidden", status = HttpStatusCode.Forbidden)
                }
            }
        })
        setupRouting(json, integrationHandler, settings)
    }
    return KtorServer(server)
}
