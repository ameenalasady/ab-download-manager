package ir.amirab.downloader.monitor

import ir.amirab.downloader.downloaditem.http.HttpDownloadJob

/** Extracted from HttpDownloaderInUi; shared byte/part/resume/ETA interpretation. */
fun createHttpProcessingState(props: ProcessingDownloadItemFactoryInputs<HttpDownloadJob>): RangeBasedProcessingDownloadItemState {
    val downloadJob = props.downloadJob
    val downloadItem = downloadJob.downloadItem
    val contentLength = downloadItem.contentLength
    return RangeBasedProcessingDownloadItemState(
        id = downloadItem.id,
        folder = downloadItem.folder,
        name = downloadItem.name,
        contentLength = contentLength,
        dateAdded = downloadItem.dateAdded,
        startTime = downloadItem.startTime ?: -1,
        completeTime = downloadItem.completeTime ?: -1,
        status = downloadJob.status.value,
        saveLocation = downloadItem.name,
        parts = downloadJob.getParts().map { UiRangedPart.fromPart(it, contentLength) },
        speed = props.speed,
        supportResume = downloadJob.supportsConcurrent,
        downloadLink = downloadItem.link,
        isWaiting = props.isWaiting,
    )
}
