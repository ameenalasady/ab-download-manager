package com.abdownloadmanager.service

import java.nio.channels.FileChannel
import java.nio.channels.FileLock
import java.nio.channels.OverlappingFileLockException
import java.nio.file.Files
import java.nio.file.LinkOption.NOFOLLOW_LINKS
import java.nio.file.Path
import java.nio.file.StandardOpenOption.*

/** Shared by Compose and the service. Acquire BEFORE constructing persistence owners.
 * Never unlink a lock file: a waiter could otherwise lock an obsolete inode.
 */
class ProfileLease private constructor(private val channel: FileChannel, private val lock: FileLock) : AutoCloseable {
    override fun close() { try { lock.release() } finally { channel.close() } }

    companion object {
        fun acquire(lockPath: Path): ProfileLease = acquire(lockPath, create = true)

        /** Offline copying must not create/change a source profile merely to inspect it. */
        fun acquireExisting(lockPath: Path): ProfileLease = acquire(lockPath, create = false)

        private fun acquire(lockPath: Path, create: Boolean): ProfileLease {
            if (create) Files.createDirectories(lockPath.toAbsolutePath().parent)
            require(!Files.isSymbolicLink(lockPath)) { "Symlinked profile lock refused" }
            if (!create) {
                RuntimePolicy.noSymlinkAncestors(lockPath)
                require(Files.isRegularFile(lockPath, NOFOLLOW_LINKS) &&
                    (Files.getAttribute(lockPath, "unix:nlink", NOFOLLOW_LINKS) as Number).toLong() == 1L) { "Missing/ambiguous source profile lock" }
            }
            val channel = if (create) FileChannel.open(lockPath, CREATE, WRITE, NOFOLLOW_LINKS)
                else FileChannel.open(lockPath, WRITE, NOFOLLOW_LINKS)
            try {
                val lock = try { channel.tryLock() } catch (_: OverlappingFileLockException) { null }
                checkNotNull(lock) { "Profile already has a persistence owner" }
                return ProfileLease(channel, lock)
            } catch (e: Throwable) { channel.close(); throw e }
        }
    }
}
