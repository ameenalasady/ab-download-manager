package com.abdownloadmanager.service

import kotlinx.coroutines.runBlocking
import kotlinx.serialization.json.*
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.attribute.BasicFileAttributes
import java.nio.file.attribute.PosixFilePermissions
import java.security.MessageDigest
import kotlin.test.*

class ProfileCopiesTest {
    private fun hash(path: Path) = MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(path)).joinToString("") { "%02x".format(it) }
    private fun write(path: Path, text: String): Path { Files.createDirectories(path.parent); return Files.writeString(path, text) }
    private fun evidence(root: Path) = Files.walk(root).use { paths -> paths.filter { Files.isRegularFile(it) }.toList().associate {
        root.relativize(it).toString() to Triple(hash(it), Files.getLastModifiedTime(it), Files.readAttributes(it, BasicFileAttributes::class.java).fileKey().toString())
    } }
    private fun fixture(test: (Path, Path) -> Unit) {
        val allowed = Files.createTempDirectory("abdm-profile-copy-")
        val source = Files.createDirectory(allowed.resolve("source"))
        ProfileLease.acquire(source.resolve("app-data/config/app.lock")).close()
        try { test(allowed, source) } finally { allowed.toFile().deleteRecursively() }
    }
    private fun choice(source: Path, target: String, temporary: Boolean) = ProfileCopies.Choice(target, temporary,
        source.resolve(target).takeIf { Files.exists(it) }?.let(::hash), hash(source.resolve("$target.tmp")))

    @Test fun originalFormatCopiesPreserveIdsMembershipSecretsUnknownBytesAndBothPartialLayouts() = fixture { allowed, source ->
        runBlocking {
            val owner = ServiceLifecycle(source)
            try {
                val engine = owner.start()
                engine.command(buildJsonObject { put("op", "add"); put("url", "http://127.0.0.1:12345/fixture"); put("name", "saved.bin") })
            } finally { owner.stop() }
        }
        val record = source.resolve("app-data/config/download_db/downloadlist/0.json")
        val original = Json.parseToJsonElement(Files.readString(record)).jsonObject
        // Future fields and >2^53 IDs must never pass through lossy typed reserialization.
        val id = 9007199254740993L
        val largeRecord = record.resolveSibling("$id.json")
        Files.move(record, largeRecord)
        Files.writeString(largeRecord, JsonObject(original + mapOf("id" to JsonPrimitive(id), "username" to JsonPrimitive("private-user"),
            "password" to JsonPrimitive("private-password"), "future" to Json.parseToJsonElement("""{"n":9007199254740995,"text":" exact ","null":null}"""))).toString() + "\n")
        write(source.resolve("app-data/config/download_db/downloadlist/last_id.txt"), id.toString())
        write(source.resolve("app-data/config/download_db/queues/11.json"), """ {"id":11,"queueItems":[$id],"future":{"keep":true}} """)
        write(source.resolve("app-data/config/download_db/categories/categories.json"), """[{"id":7,"items":[$id],"future":"retained"}]""")
        write(source.resolve("app-data/config/appSettings.json"), """ {"threadCount":"corrupt-preserved","apiAuthKey":"private-secret","unknown":[1,null]} """)
        write(source.resolve("app-data/config/download_db/extra_download_settings/$id.json"), """{"id":$id,"powerActionTypeOnFinish":"Shutdown","future":true}""")
        write(source.resolve("app-data/system/downloadData/opaque.bin"), "opaque system state")
        Files.createDirectories(source.resolve("empty-future-directory"))
        val payload = Files.createDirectory(allowed.resolve("external-payload"))
        Files.write(source.resolve("Downloads/no-extension.bin"), byteArrayOf(0, 1, 2, -1))
        Files.write(payload.resolve("saved.bin.dl-$id.abdm.part"), ByteArray(1009) { (it % 251).toByte() })
        val before = evidence(source); val payloadBefore = evidence(payload)
        val destination = allowed.resolve("copy")
        val report = ProfileCopies.create(ProfileCopies.Request(source, destination, listOf(payload)), allowed)
        assertEquals(before, evidence(source)); assertEquals(payloadBefore, evidence(payload))
        assertEquals(report, ProfileCopies.verify(destination, allowed))
        assertEquals("quarantined-copy-unvalidated", report.getValue("outcome").jsonPrimitive.content)
        assertFalse(report.getValue("executionAllowed").jsonPrimitive.boolean)
        assertFalse(report.getValue("externalPayloadsComplete").jsonPrimitive.boolean)
        assertTrue(Files.isDirectory(destination.resolve("original/empty-future-directory")))
        before.filterKeys { it != "app-data/config/app.lock" }.forEach { (path, e) ->
            val copied = destination.resolve("original/$path")
            assertEquals(e.first, hash(copied), path); assertEquals(e.second, Files.getLastModifiedTime(copied), path)
            assertNotEquals(e.third, Files.readAttributes(copied, BasicFileAttributes::class.java).fileKey().toString(), path)
        }
        assertFalse(Files.exists(destination.resolve("original/app-data/config/app.lock")))
        assertEquals(hash(payload.resolve("saved.bin.dl-$id.abdm.part")), hash(destination.resolve("payloads/0/saved.bin.dl-$id.abdm.part")))
        assertEquals(hash(destination.resolve("manifest.json")), Files.readString(destination.resolve("COMPLETE")).trim())
        assertFalse(Files.readString(destination.resolve("manifest.json")).contains("private-password"))
        Files.walk(destination).use { paths -> paths.forEach { p ->
            val expected = if (Files.isDirectory(p)) "rwx------" else "rw-------"
            assertEquals(PosixFilePermissions.fromString(expected), Files.getPosixFilePermissions(p), p.toString())
        } }
    }

    @Test fun explicitRecoveryRetainsBothVersionsAndSelectsOnlyHashBoundBytes() = fixture { allowed, source ->
        val settings = "app-data/config/appSettings.json"
        val parts = "app-data/config/download_db/parts/42.json"
        write(source.resolve(settings), " {\"threadCount\":4,\"future\":true} ")
        write(source.resolve("$settings.tmp"), "{\"threadCount\":8,\"private\":\"retained\"}\n")
        write(source.resolve(parts), "committed part evidence")
        write(source.resolve("$parts.tmp"), "interrupted part evidence")
        val before = evidence(source)
        val destination = allowed.resolve("recovery")
        val choices = listOf(choice(source, settings, true), choice(source, parts, false))
        val report = ProfileCopies.create(ProfileCopies.Request(source, destination, choices = choices), allowed)
        assertEquals(before, evidence(source))
        assertEquals(report, ProfileCopies.verify(destination, allowed))
        assertEquals("quarantined-recovery-candidate-unvalidated", report.getValue("outcome").jsonPrimitive.content)
        assertEquals(hash(source.resolve("$settings.tmp")), hash(destination.resolve("candidate/$settings")))
        assertEquals(hash(source.resolve(parts)), hash(destination.resolve("candidate/$parts")))
        for (target in listOf(settings, parts)) {
            assertEquals(hash(source.resolve(target)), hash(destination.resolve("original/$target")))
            assertEquals(hash(source.resolve("$target.tmp")), hash(destination.resolve("original/$target.tmp")))
            assertFalse(Files.exists(destination.resolve("candidate/$target.tmp")))
        }
    }

    @Test fun temporaryOnlyRecoveryIsExplicitAndNeverInventsMissingCommittedBytes() = fixture { allowed, source ->
        val target = "app-data/config/download_db/downloadlist/last_id.txt"
        write(source.resolve("$target.tmp"), "9007199254740993")
        assertFails { ProfileCopies.create(ProfileCopies.Request(source, allowed.resolve("bad"), choices = listOf(choice(source, target, false))), allowed) }
        assertFalse(Files.exists(allowed.resolve("bad")))
        ProfileCopies.create(ProfileCopies.Request(source, allowed.resolve("selected"), choices = listOf(choice(source, target, true))), allowed)
        assertEquals("9007199254740993", Files.readString(allowed.resolve("selected/candidate/$target")))
        assertFalse(Files.exists(allowed.resolve("selected/original/$target")))
    }

    @Test fun staleIncompleteDuplicateUnsupportedAndTraversalChoicesRejectBeforeCreatingOutput() = fixture { allowed, source ->
        val target = "app-data/config/appSettings.json"
        write(source.resolve(target), "old"); write(source.resolve("$target.tmp"), "new")
        val valid = choice(source, target, true)
        val requests = listOf(listOf(valid.copy(temporarySha256 = "0".repeat(64))), listOf(valid.copy(committedSha256 = null)), listOf(valid, valid),
            listOf(valid.copy(target = "../outside")), listOf(valid.copy(target = "/absolute")))
        requests.forEachIndexed { index, choices ->
            val destination = allowed.resolve("bad-$index")
            assertFails { ProfileCopies.create(ProfileCopies.Request(source, destination, choices = choices), allowed) }
            assertFalse(Files.exists(destination))
        }
        write(source.resolve("app-data/config/unknown.json.tmp"), "unknown transaction")
        assertFails { ProfileCopies.create(ProfileCopies.Request(source, allowed.resolve("incomplete"), choices = listOf(valid)), allowed) }
        assertFails { ProfileCopies.create(ProfileCopies.Request(source, allowed.resolve("unsupported"), choices = listOf(valid, choice(source, "app-data/config/unknown.json", true))), allowed) }
        // A preservation-only copy may archive arbitrary corrupt/ambiguous bytes, but is NOT bootable or labelled repaired.
        ProfileCopies.create(ProfileCopies.Request(source, allowed.resolve("archive")), allowed)
        assertEquals("unknown transaction", Files.readString(allowed.resolve("archive/original/app-data/config/unknown.json.tmp")))
    }

    @Test fun competingWriterRejectsBeforeDestinationAndLockInodeSurvives() = fixture { allowed, source ->
        val lock = source.resolve("app-data/config/app.lock")
        val before = evidence(source)
        ProfileLease.acquire(lock).use {
            assertFails { ProfileCopies.create(ProfileCopies.Request(source, allowed.resolve("busy")), allowed) }
        }
        assertFalse(Files.exists(allowed.resolve("busy"))); assertEquals(before, evidence(source))
        ProfileCopies.create(ProfileCopies.Request(source, allowed.resolve("okay")), allowed)
        ProfileLease.acquireExisting(lock).close()
        assertEquals(before, evidence(source))
    }

    @Test fun missingLockIsNotCreatedAndInvalidPathsNeverBecomeImports() {
        val allowed = Files.createTempDirectory("abdm-copy-policy-")
        try {
            val source = Files.createDirectory(allowed.resolve("source"))
            assertFails { ProfileCopies.create(ProfileCopies.Request(source, allowed.resolve("missing-lock")), allowed) }
            assertFalse(Files.exists(source.resolve("app-data")))
            ProfileLease.acquire(source.resolve("app-data/config/app.lock")).close()
            Files.createSymbolicLink(allowed.resolve("alias"), source)
            for (request in listOf(ProfileCopies.Request(allowed.resolve("alias"), allowed.resolve("bad")),
                ProfileCopies.Request(source, source.resolve("nested")), ProfileCopies.Request(source, allowed.resolve("alias/bad")),
                ProfileCopies.Request(source, allowed.resolve("../outside")), ProfileCopies.Request(source, allowed.resolve("bad"), listOf(source)))) {
                assertFails { ProfileCopies.create(request, allowed) }
            }
            val existing = write(allowed.resolve("existing"), "do not overwrite")
            assertFails { ProfileCopies.create(ProfileCopies.Request(source, existing), allowed) }
            assertEquals("do not overwrite", Files.readString(existing))
        } finally { allowed.toFile().deleteRecursively() }
    }

    @Test fun symlinksHardlinksAndSpecialFilesRefuseWithoutDestination() = fixture { allowed, source ->
        val protected = write(allowed.resolve("protected"), "untouched")
        val alias = source.resolve("alias")
        Files.createSymbolicLink(alias, protected)
        assertFails { ProfileCopies.create(ProfileCopies.Request(source, allowed.resolve("symlink")), allowed) }
        Files.delete(alias)
        Files.createLink(alias, protected)
        assertFails { ProfileCopies.create(ProfileCopies.Request(source, allowed.resolve("hardlink")), allowed) }
        Files.delete(alias)
        assertEquals(0, ProcessBuilder("mkfifo", alias.toString()).start().waitFor())
        assertFails { ProfileCopies.create(ProfileCopies.Request(source, allowed.resolve("fifo")), allowed) }
        assertEquals("untouched", Files.readString(protected))
        for (name in listOf("symlink", "hardlink", "fifo")) assertFalse(Files.exists(allowed.resolve(name)))
    }

    @Test fun interruptedCopyIsRetainedCannotBeReusedAndNeverPublishesComplete() = fixture { allowed, source ->
        write(source.resolve("unknown.bin"), "opaque bytes")
        val before = evidence(source)
        for (stage in listOf("created", "original-copied", "copied", "manifest-written")) {
            val destination = allowed.resolve(stage)
            assertFails { ProfileCopies.create(ProfileCopies.Request(source, destination), allowed) { if (it == stage) error("injected") } }
            assertTrue(Files.exists(destination.resolve("STARTED"))); assertFalse(Files.exists(destination.resolve("COMPLETE")))
            assertFails { ProfileCopies.verify(destination, allowed) }
            val partial = evidence(destination)
            assertFails { ProfileCopies.create(ProfileCopies.Request(source, destination), allowed) }
            assertEquals(partial, evidence(destination)); assertEquals(before, evidence(source))
            ProfileLease.acquireExisting(source.resolve("app-data/config/app.lock")).close()
        }
    }

    @Test fun detectedSourceChangeReportsIncompleteWithoutErasingAnything() = fixture { allowed, source ->
        val file = write(source.resolve("unknown.bin"), "before")
        val destination = allowed.resolve("changed")
        assertFails { ProfileCopies.create(ProfileCopies.Request(source, destination), allowed) { if (it == "copied") Files.writeString(file, "changed by fixture") } }
        assertFalse(Files.exists(destination.resolve("COMPLETE")))
        assertEquals("before", Files.readString(destination.resolve("original/unknown.bin")))
        assertEquals("changed by fixture", Files.readString(file))
    }

    @Test fun changedOutputCannotBeCertifiedAsAnExactSourceCopy() = fixture { allowed, source ->
        write(source.resolve("unknown.bin"), "original")
        val before = evidence(source)
        val destination = allowed.resolve("changed-output")
        assertFails { ProfileCopies.create(ProfileCopies.Request(source, destination), allowed) { stage ->
            if (stage == "copied") {
                val copied = destination.resolve("original/unknown.bin")
                val modified = Files.getLastModifiedTime(copied)
                Files.writeString(copied, "tampered"); Files.setLastModifiedTime(copied, modified)
            }
        } }
        assertFalse(Files.exists(destination.resolve("COMPLETE")))
        assertEquals(before, evidence(source))
        assertEquals("tampered", Files.readString(destination.resolve("original/unknown.bin")))
    }

    @Test fun quarantinedCopiesRefuseLifecycleAndDirectEngineConstructionBeforeAnyWrites() = fixture { allowed, source ->
        val target = "app-data/config/appSettings.json"
        write(source.resolve(target), "corrupt-original"); write(source.resolve("$target.tmp"), "also-corrupt")
        write(source.resolve("qt-runtime-policy.json"), "{\"profile\":\"$source\"}")
        val destination = allowed.resolve("blocked")
        ProfileCopies.create(ProfileCopies.Request(source, destination, choices = listOf(choice(source, target, true))), allowed)
        val before = evidence(destination)
        for (profile in listOf(destination, destination.resolve("original"), destination.resolve("candidate"), destination.resolve("original/nested"))) {
            runBlocking {
                val owner = ServiceLifecycle(profile)
                assertFailsWith<IllegalArgumentException> { owner.start() }
                owner.stop()
            }
            assertFailsWith<IllegalArgumentException> { EngineSession(profile) }
        }
        assertFailsWith<IllegalArgumentException> { RuntimePolicy.load(destination.resolve("original/qt-runtime-policy.json")) }
        assertFailsWith<IllegalArgumentException> { RuntimePolicy.load(destination.resolve("candidate/qt-runtime-policy.json")) }
        assertEquals(before, evidence(destination))
    }

    @Test fun oversizedSparseSourceRejectsBeforeCopyWithoutReadingOrTruncatingIt() = fixture { allowed, source ->
        val file = source.resolve("oversized")
        java.io.RandomAccessFile(file.toFile(), "rw").use { it.setLength(1024L * 1024 * 1024 + 1) }
        assertFails { ProfileCopies.create(ProfileCopies.Request(source, allowed.resolve("oversized-copy")), allowed) }
        assertFalse(Files.exists(allowed.resolve("oversized-copy")))
        assertEquals(1024L * 1024 * 1024 + 1, Files.size(file))
    }

    @Test fun verificationRejectsTamperedBytesManifestExtraEntriesAndMissingQuarantine() = fixture { allowed, source ->
        write(source.resolve("unknown.bin"), "original")
        for (case in listOf("bytes", "manifest", "extra", "quarantine", "payload-alias", "permissions", "root-permissions")) {
            val destination = allowed.resolve(case)
            val payload = Files.createDirectory(allowed.resolve("payload-$case"))
            ProfileCopies.create(ProfileCopies.Request(source, destination, listOf(payload)), allowed)
            when (case) {
                "bytes" -> {
                    val file = destination.resolve("original/unknown.bin"); val modified = Files.getLastModifiedTime(file)
                    Files.writeString(file, "tampered"); Files.setLastModifiedTime(file, modified)
                }
                "manifest" -> Files.writeString(destination.resolve("manifest.json"), "{}")
                "extra" -> write(destination.resolve("extra"), "unexpected")
                "quarantine" -> Files.delete(destination.resolve("original/${ProfileCopies.QUARANTINE}"))
                "permissions" -> Files.setPosixFilePermissions(destination.resolve("original/unknown.bin"), PosixFilePermissions.fromString("rw-r--r--"))
                "root-permissions" -> Files.setPosixFilePermissions(destination, PosixFilePermissions.fromString("rwxr-xr-x"))
                "payload-alias" -> {
                    Files.delete(destination.resolve("payloads/0")); Files.delete(destination.resolve("payloads"))
                    Files.createSymbolicLink(destination.resolve("payloads"), source)
                }
            }
            val before = evidence(source)
            assertFails { ProfileCopies.verify(destination, allowed) }
            assertEquals(before, evidence(source))
        }
    }

    @Test fun requestParserRequiresPrivateStrictExplicitFields() = fixture { allowed, source ->
        val file = write(allowed.resolve("request.json"), buildJsonObject {
            put("source", source.toString()); put("destination", allowed.resolve("copy").toString()); put("payloadRoots", JsonArray(emptyList())); put("choices", JsonArray(emptyList()))
        }.toString())
        Files.setPosixFilePermissions(file, PosixFilePermissions.fromString("rw-------"))
        assertEquals(source, ProfileCopies.readRequest(file).source)
        Files.setPosixFilePermissions(file, PosixFilePermissions.fromString("rw-r--r--"))
        assertFails { ProfileCopies.readRequest(file) }
        Files.setPosixFilePermissions(file, PosixFilePermissions.fromString("rw-------"))
        Files.writeString(file, "{}")
        assertFails { ProfileCopies.readRequest(file) }
    }
}
