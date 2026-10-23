package com.abdownloadmanager.desktop.experimental

import com.abdownloadmanager.updateapplier.UpdateApplier
import com.abdownloadmanager.updatechecker.UpdateInfo

/** No self-install, extraction or cleanup in an experimental session. */
object ExperimentalUpdateApplier : UpdateApplier {
    override fun updateSupported() = false
    override suspend fun applyUpdate(updateInfo: UpdateInfo): Unit = error("Updates disabled in experimental sessions")
    override suspend fun cleanup() = Unit
}
