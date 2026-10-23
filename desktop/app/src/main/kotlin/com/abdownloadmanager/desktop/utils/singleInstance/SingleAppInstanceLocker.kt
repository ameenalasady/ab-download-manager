package com.abdownloadmanager.desktop.utils.singleInstance

import okio.Path
import com.abdownloadmanager.service.ProfileLease
import kotlin.concurrent.thread

class SingleAppInstanceLocker(
    private val lockPath: Path
) {
    private fun getLockPath(): Path {
        return lockPath
    }

    /**
     * @throws AnotherInstanceIsRunning
     */
    fun tryLockInstance() {
        val lease = try {
            ProfileLease.acquire(getLockPath().toNioPath())
        } catch (e: IllegalStateException) {
            throw AnotherInstanceIsRunning()
        }
        Runtime.getRuntime().addShutdownHook(thread(start = false) { lease.close() })

    }
}

class AnotherInstanceIsRunning(
) : RuntimeException("Another Instance Is Running")
