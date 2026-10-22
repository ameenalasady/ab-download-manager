package com.abdownloadmanager.shared.storage

import ir.amirab.downloader.db.TransactionalFileSaver
import kotlinx.coroutines.flow.Flow
import java.io.File

class ExtraDownloadSettingsStorage<T : IExtraDownloadItemSettings>(
    folder: File,
    transactionalFileSaver: TransactionalFileSaver,
    dataClassDefinitions: IExtraDownloadItemSettings.DataClassDefinitions<T>,
    strictReads: Boolean = false,
) : IExtraDownloadSettingsStorage<T> {
    private val store = ExtraSettingsFileStore(folder, transactionalFileSaver,
        dataClassDefinitions.serializer, dataClassDefinitions::createDefault, { value: T -> value.id }, strictReads)
    fun validateStored() = store.validateStored()
    override suspend fun deleteExtraDownloadItemSettings(downloadId: Long) = store.delete(downloadId)
    override suspend fun setExtraDownloadItemSettings(extraDownloadItemSettings: T) = store.write(extraDownloadItemSettings)
    override fun getExtraDownloadItemSettings(downloadId: Long): T = store.read(downloadId)
    override fun getExternalDownloadItemSettingsAsFlow(id: Long, initialEmit: Boolean): Flow<T> = store.flow(id, initialEmit)
}
