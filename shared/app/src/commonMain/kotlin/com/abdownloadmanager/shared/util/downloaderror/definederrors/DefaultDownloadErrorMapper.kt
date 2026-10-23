package com.abdownloadmanager.shared.util.downloaderror.definederrors

import com.abdownloadmanager.resources.Res
import com.abdownloadmanager.shared.util.downloaderror.DownloadErrorMapper
import com.abdownloadmanager.shared.util.downloaderror.FallbackDownloadErrorMapper
import ir.amirab.util.compose.asStringSource
import ir.amirab.util.compose.asStringSourceWithARgs

/** Keep Compose's localized resources; policy is shared with the service. */
object DefaultDownloadErrorMapper : DownloadErrorMapper by FallbackDownloadErrorMapper(
    unknown = { Res.string.unknown.asStringSource().getString() },
    title = { Res.string.download_error_reason_default_title.asStringSource().getString() },
    description = { name, message ->
        Res.string.download_error_reason_default_description.asStringSourceWithARgs(
            Res.string.download_error_reason_default_description_createArgs(
                exceptionName = name, exceptionMessage = message,
            )
        ).getString()
    },
    suggestion = { Res.string.download_error_reason_default_suggestion.asStringSource().getString() },
)
