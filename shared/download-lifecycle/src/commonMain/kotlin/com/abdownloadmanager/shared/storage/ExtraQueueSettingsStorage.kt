package com.abdownloadmanager.shared.storage

import ir.amirab.downloader.db.TransactionalFileSaver
import kotlinx.coroutines.flow.Flow
import java.io.File

class ExtraQueueSettingsStorage<T : IExtraQueueSettings>(
    folder: File,
    transactionalFileSaver: TransactionalFileSaver,
    dataClassDefinitions: IExtraQueueSettings.DataClassDefinitions<T>,
    strictReads: Boolean = false,
) : IExtraQueueSettingsStorage<T> {
    private val store = ExtraSettingsFileStore(folder, transactionalFileSaver,
        dataClassDefinitions.serializer, dataClassDefinitions::createDefault, { value: T -> value.id }, strictReads)
    fun validateStored() = store.validateStored()
    override suspend fun deleteExtraQueueSettings(queueId: Long) = store.delete(queueId)
    override suspend fun setExtraQueueSettings(extraQueueSettings: T) = store.write(extraQueueSettings)
    override fun getExtraQueueSettings(queueId: Long): T = store.read(queueId)
    override fun getExternalQueueSettingsAsFlow(id: Long, initialEmit: Boolean): Flow<T> = store.flow(id, initialEmit)
}
