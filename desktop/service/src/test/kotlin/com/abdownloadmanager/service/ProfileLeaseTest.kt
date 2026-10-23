package com.abdownloadmanager.service

import java.nio.file.Files
import java.nio.file.attribute.BasicFileAttributes
import kotlin.test.*

class ProfileLeaseTest {
    @Test fun exclusiveAndNeverUnlinksLockInode() {
        val root = Files.createTempDirectory("abdm-lease-test-")
        val path = root.resolve("config/app.lock")
        try {
            val first = ProfileLease.acquire(path)
            val key = Files.readAttributes(path, BasicFileAttributes::class.java).fileKey()
            assertFailsWith<IllegalStateException> { ProfileLease.acquire(path) }
            first.close()
            assertTrue(Files.exists(path))
            ProfileLease.acquire(path).use {
                assertEquals(key, Files.readAttributes(path, BasicFileAttributes::class.java).fileKey())
                assertFailsWith<IllegalStateException> { ProfileLease.acquire(path) }
            }
        } finally { root.toFile().deleteRecursively() }
    }
    @Test fun symlinkLockRefusedWithoutChangingTarget() {
        val root = Files.createTempDirectory("abdm-lease-test-")
        try {
            val target = root.resolve("protected")
            Files.writeString(target, "unchanged")
            val path = root.resolve("app.lock")
            Files.createSymbolicLink(path, target)
            assertFailsWith<IllegalArgumentException> { ProfileLease.acquire(path) }
            assertEquals("unchanged", Files.readString(target))
        } finally { root.toFile().deleteRecursively() }
    }
}
