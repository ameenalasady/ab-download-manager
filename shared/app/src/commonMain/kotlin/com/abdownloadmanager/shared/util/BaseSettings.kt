package com.abdownloadmanager.shared.util

import androidx.datastore.core.DataStore
import arrow.optics.Lens
import ir.amirab.util.flow.mapTwoWayStateFlow
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import ir.amirab.util.logger.appLogger
import org.koin.core.component.KoinComponent
import org.koin.core.component.inject
import kotlin.time.Duration.Companion.milliseconds

abstract class BaseStorage<T> : KoinComponent {
    val scope: CoroutineScope by inject()

    protected abstract val inMemoryState: MutableStateFlow<T>
    protected abstract suspend fun saveData(data: T)

    private val saveMutex = Mutex()
    val data get() = inMemoryState

    fun <K> from(lens: Lens<T, K>): MutableStateFlow<K> {
        return inMemoryState.mapTwoWayStateFlow(lens)
    }

    /**
     * call this on upper implementations where [inMemoryState] and [saveData] are implemented
     */
    protected fun startPersistData() {
        inMemoryState
            //first
            .drop(1)
            .debounce(500.milliseconds)
            .onEach {
                saveCurrentState()
            }.launchIn(scope)
    }

    /**
     * [startPersistData] debounces the writes, and the app kills its own process when it exits,
     * so everything that changed in the last moments would never reach the disk.
     * this writes the current state immediately instead of waiting for the debounce.
     */
    suspend fun flush() {
        try {
            saveCurrentState()
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (error: Exception) {
            appLogger.e { "Could not flush settings: ${error.message}" }
        }
    }

    private suspend fun saveCurrentState() {
        // A queued debounce must not overwrite a newer shutdown flush with an old snapshot.
        saveMutex.withLock { saveData(inMemoryState.value) }
    }
}

abstract class ConfigBaseSettingsByJson<T>(
    private val dataStore: DataStore<T>,
) : BaseStorage<T>(), KoinComponent {
    private val lastFileState = dataStore.data.let {
        runBlocking { it.stateIn(scope) }
    }

    override val inMemoryState = MutableStateFlow(lastFileState.value)
    override suspend fun saveData(data: T) {
        dataStore.updateData { data }
    }

    init {
        startPersistData()
    }
}
