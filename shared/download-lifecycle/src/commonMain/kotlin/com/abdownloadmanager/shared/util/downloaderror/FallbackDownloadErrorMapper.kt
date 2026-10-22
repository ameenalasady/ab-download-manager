package com.abdownloadmanager.shared.util.downloaderror

/** Original default mapper's policy, with frontend-owned message rendering. */
class FallbackDownloadErrorMapper(
    private val unknown: () -> String,
    private val title: () -> String,
    private val description: (exceptionName: String, exceptionMessage: String) -> String,
    private val suggestion: () -> String,
) : DownloadErrorMapper {
    override fun accept(throwable: Throwable) = true

    override fun getReason(throwable: Throwable) = DownloadErrorMapper.createErrorReason(
        title = title(),
        description = description(throwable::class.simpleName ?: unknown(), throwable.localizedMessage ?: unknown()),
        suggestion = suggestion(),
        throwable = throwable,
    )
}
