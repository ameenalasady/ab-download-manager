package com.abdownloadmanager.shared.util.downloaderror

import kotlinx.serialization.Serializable

@Serializable
data class DownloadErrorReason(
    val title: String,
    val description: String,
    val suggestion: String,
    val throwableName: String,
    val throwableMessage: String?,
)
