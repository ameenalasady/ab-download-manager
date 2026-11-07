package com.abdownloadmanager.service

import com.abdownloadmanager.shared.util.downloaderror.FallbackDownloadErrorMapper
import java.util.Properties

/** English fallback wording for download errors, taken from the original app's en_US strings. */
internal fun serviceFallbackErrorMapper(): FallbackDownloadErrorMapper {
    val messages = Properties().apply {
        val input = checkNotNull(EngineSession::class.java.getResourceAsStream("/download-errors-en.properties"))
        input.reader(Charsets.UTF_8).use { load(it) }
    }
    fun text(key: String) = checkNotNull(messages.getProperty(key))
    return FallbackDownloadErrorMapper(
        unknown = { text("unknown") },
        title = { text("download_error_reason_default_title") },
        description = { name, message -> text("download_error_reason_default_description")
            .replace("{{exceptionName}}", name).replace("{{exceptionMessage}}", message) },
        suggestion = { text("download_error_reason_default_suggestion") },
    )
}
