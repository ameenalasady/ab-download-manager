package com.abdownloadmanager.service

import kotlinx.serialization.json.*
import java.nio.channels.FileChannel
import java.nio.file.Files
import java.nio.file.LinkOption.NOFOLLOW_LINKS
import java.nio.file.Path
import java.nio.file.StandardOpenOption.*
import java.nio.file.attribute.BasicFileAttributes
import java.nio.file.attribute.FileTime
import java.nio.file.attribute.PosixFilePermissions
import java.security.MessageDigest

/** Offline fixture-only preservation workspace, NOT a profile converter or engine validator.
 * Never boot stores/schedulers to inspect an import. Both original and candidate stay
 * quarantined, with absolute stored destinations unchanged and payloads archived separately.
 * Not a sandbox against a malicious same-UID process; all fixture writers must be stopped.
 */
internal object ProfileCopies {
    const val QUARANTINE = ".qt-copy-quarantine"
    private const val LOCK = "app-data/config/app.lock"
    private const val MAX_FILES = 100_000
    private const val MAX_BYTES = 1024L * 1024 * 1024
    private val privateDirectory = PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rwx------"))
    private val privateFile = PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rw-------"))

    data class Choice(val target: String, val useTemporary: Boolean, val committedSha256: String?, val temporarySha256: String)
    data class Request(val source: Path, val destination: Path, val payloadRoots: List<Path> = emptyList(), val choices: List<Choice> = emptyList())
    private data class Entry(val path: String, val size: Long, val modified: FileTime, val key: String, val sha256: String)
    private data class Tree(val directories: List<String>, val files: List<Entry>)

    /** This gate also applies when callers bypass the CLI and construct a lifecycle directly. */
    fun requireNotQuarantined(profile: Path) {
        var ancestor: Path? = profile.toAbsolutePath().normalize()
        while (ancestor != null) {
            require(!Files.exists(ancestor.resolve(QUARANTINE), NOFOLLOW_LINKS)) {
                "Copied/recovery profiles are quarantined; execution and automatic recovery are forbidden"
            }
            ancestor = ancestor.parent
        }
    }

    fun readRequest(file: Path): Request {
        RuntimePolicy.noSymlinkAncestors(file)
        require(Files.isRegularFile(file, NOFOLLOW_LINKS) && Files.size(file) <= 128 * 1024) { "Invalid copy request" }
        require(Files.getPosixFilePermissions(file).none { it in PosixFilePermissions.fromString("---rwxrwx") }) { "Copy request must be private" }
        val doc = Json.parseToJsonElement(Files.readString(file)).jsonObject
        require(doc.keys == setOf("source", "destination", "payloadRoots", "choices")) { "Invalid copy request keys" }
        fun text(value: JsonElement): String { require(value is JsonPrimitive && value.isString); return value.content }
        return Request(Path.of(text(doc.getValue("source"))), Path.of(text(doc.getValue("destination"))),
            doc.getValue("payloadRoots").jsonArray.map { Path.of(text(it)) },
            doc.getValue("choices").jsonArray.map { element ->
                val c = element.jsonObject
                require(c.keys == setOf("target", "use", "committedSha256", "temporarySha256")) { "Invalid recovery choice keys" }
                val use = text(c.getValue("use")); require(use in setOf("committed", "temporary")) { "Explicit byte selection required" }
                Choice(text(c.getValue("target")), use == "temporary",
                    c.getValue("committedSha256").takeUnless { it == JsonNull }?.let(::text), text(c.getValue("temporarySha256")))
            })
    }

    private fun fixturePath(path: Path, allowed: Path) {
        require(path.isAbsolute && path.normalize() == path && path.startsWith(allowed) && path != allowed) { "Only disposable build/qt-service paths are allowed" }
        RuntimePolicy.noSymlinkAncestors(path)
    }

    private fun digest(path: Path): String {
        val digest = MessageDigest.getInstance("SHA-256")
        Files.newInputStream(path, READ, NOFOLLOW_LINKS).use { stream ->
            val buffer = ByteArray(128 * 1024)
            while (true) { val count = stream.read(buffer); if (count < 0) break; digest.update(buffer, 0, count) }
        }
        return digest.digest().joinToString("") { "%02x".format(it) }
    }

    private fun inspect(root: Path, profile: Boolean, privateCopy: Boolean = false): Tree {
        val directories = mutableListOf<String>()
        val files = mutableListOf<Entry>()
        var total = 0L
        val owner = Files.getOwner(root, NOFOLLOW_LINKS)
        val currentUser = root.fileSystem.userPrincipalLookupService.lookupPrincipalByName(System.getProperty("user.name"))
        require(owner == currentUser) { "Fixture owner mismatch" }
        Files.walk(root, 65).use { paths -> paths.forEach { path ->
            val relative = root.relativize(path).toString()
            val a = Files.readAttributes(path, BasicFileAttributes::class.java, NOFOLLOW_LINKS)
            require(root.relativize(path).nameCount <= 64) { "Fixture tree too deep" }
            require(!a.isSymbolicLink && (a.isDirectory || a.isRegularFile)) { "Symlink/special fixture entry refused" }
            require(Files.getOwner(path, NOFOLLOW_LINKS) == owner) { "Fixture entry owner mismatch" }
            if (privateCopy) require(Files.getPosixFilePermissions(path, NOFOLLOW_LINKS) ==
                PosixFilePermissions.fromString(if (a.isDirectory) "rwx------" else "rw-------")) { "Copy permissions are no longer private" }
            require(directories.size + files.size < MAX_FILES) { "Fixture entry limit exceeded" }
            if (a.isDirectory) directories += relative
            else {
                require((Files.getAttribute(path, "unix:nlink", NOFOLLOW_LINKS) as Number).toLong() == 1L) { "Hard-linked fixture entry refused" }
                if (!(profile && relative == LOCK)) {
                    require(!(profile && relative == QUARANTINE)) { "Already quarantined source refused" }
                    total += a.size(); require(total <= MAX_BYTES) { "Fixture byte limit exceeded" }
                    val hash = digest(path)
                    val after = Files.readAttributes(path, BasicFileAttributes::class.java, NOFOLLOW_LINKS)
                    require(a.size() == after.size() && a.lastModifiedTime() == after.lastModifiedTime() && a.fileKey() == after.fileKey()) { "Fixture changed during inventory" }
                    files += Entry(relative, a.size(), a.lastModifiedTime(), a.fileKey().toString(), hash)
                }
            }
        } }
        return Tree(directories.sorted(), files.sortedBy { it.path })
    }

    private fun directory(path: Path) { Files.createDirectory(path, privateDirectory) }
    private fun write(path: Path, text: String) {
        FileChannel.open(path, setOf(CREATE_NEW, WRITE, NOFOLLOW_LINKS), privateFile).use { channel ->
            val bytes = java.nio.ByteBuffer.wrap(text.toByteArray(Charsets.UTF_8))
            while (bytes.hasRemaining()) channel.write(bytes)
            channel.force(true)
        }
    }
    private fun copyFile(source: Path, destination: Path, entry: Entry) {
        Files.newInputStream(source, READ, NOFOLLOW_LINKS).use { input ->
            FileChannel.open(destination, setOf(CREATE_NEW, WRITE, NOFOLLOW_LINKS), privateFile).use { output ->
                val buffer = ByteArray(128 * 1024)
                var size = 0L
                while (true) {
                    val count = input.read(buffer); if (count < 0) break
                    size += count; require(size <= entry.size) { "Fixture grew during copy" }
                    val bytes = java.nio.ByteBuffer.wrap(buffer, 0, count)
                    while (bytes.hasRemaining()) output.write(bytes)
                }
                require(size == entry.size) { "Fixture shrank during copy" }
                output.force(true)
            }
        }
        Files.setLastModifiedTime(destination, entry.modified)
        require(digest(destination) == entry.sha256) { "Copied bytes differ from inventory" }
    }
    private fun copyTree(source: Path, target: Path, tree: Tree, profile: Boolean, choices: Map<String, Choice> = emptyMap()) {
        directory(target)
        if (profile) write(target.resolve(QUARANTINE), "Quarantined preservation/recovery copy. NOT approved for execution.\n")
        tree.directories.filter { it.isNotEmpty() }.sortedWith(compareBy({ Path.of(it).nameCount }, { it })).forEach { directory(target.resolve(it)) }
        val byPath = tree.files.associateBy { it.path }
        tree.files.forEach { original ->
            if (original.path.endsWith(".tmp") && choices.containsKey(original.path.removeSuffix(".tmp"))) return@forEach
            val selected = if (choices[original.path]?.useTemporary == true) byPath.getValue(original.path + ".tmp") else original
            copyFile(source.resolve(selected.path), target.resolve(original.path), selected)
        }
        // A selected temporary can explicitly restore a missing committed document in this
        // UNVALIDATED candidate only. Its original bytes still live in original/<target>.tmp.
        choices.values.filter { it.useTemporary && it.target !in byPath }.forEach { choice ->
            copyFile(source.resolve(choice.target + ".tmp"), target.resolve(choice.target), byPath.getValue(choice.target + ".tmp"))
        }
    }

    private fun validateChoices(tree: Tree, choices: List<Choice>): Map<String, Choice> {
        if (choices.isEmpty()) return emptyMap() // Preservation only: no transaction disposition.
        val byPath = tree.files.associateBy { it.path }
        val map = choices.associateBy { it.target }
        require(map.size == choices.size) { "Duplicate recovery choice" }
        val pending = byPath.keys.filter { it.endsWith(".tmp") }.map { it.removeSuffix(".tmp") }.toSet()
        require(map.keys == pending) { "Every interrupted transaction needs an explicit choice; no guesses" }
        val supported = Regex("app-data/config/(appSettings\\.json|download_db/(downloadlist/(last_id\\.txt|(?:0|[1-9][0-9]*)\\.json)|(?:parts|queues|extra_download_settings|extra_queue_settings)/(?:0|[1-9][0-9]*)\\.json|categories/categories\\.json))")
        map.forEach { (target, choice) ->
            require(supported.matches(target)) { "Unsupported recovery transaction path" }
            require(choice.temporarySha256.matches(Regex("[a-f0-9]{64}")) && byPath.getValue("$target.tmp").sha256 == choice.temporarySha256 &&
                byPath[target]?.sha256 == choice.committedSha256) { "Recovery choice is stale or ambiguous" }
            require(choice.useTemporary || target in byPath) { "Missing committed document cannot be selected" }
        }
        return map
    }

    private fun validateCopy(source: Tree, copied: Tree, profile: Boolean, choices: Map<String, Choice> = emptyMap()) {
        val expected = source.files.associateBy { it.path }.toMutableMap()
        choices.forEach { (target, choice) ->
            val temporary = expected.remove("$target.tmp") ?: error("Missing transaction inventory")
            if (choice.useTemporary) expected[target] = temporary.copy(path = target)
        }
        val actual = copied.files.filterNot { profile && it.path == QUARANTINE }
        require(source.directories == copied.directories &&
            expected.values.sortedBy { it.path }.map { it.copy(key = "") } == actual.map { it.copy(key = "") }) { "Final copied bytes/metadata differ from selected source inventory" }
    }

    private fun inventory(tree: Tree) = buildJsonObject {
        put("directories", JsonArray(tree.directories.map(::JsonPrimitive)))
        put("files", JsonArray(tree.files.map { e -> buildJsonObject {
            put("path", e.path); put("size", e.size.toString()); put("modified", e.modified.toString()); put("sha256", e.sha256)
        } }))
    }

    /** Integrity verification only: no engine/schema validation or permission to execute.
     * COMPLETE is a hash-bound completion record, never authority to promote transactions.
     */
    fun verify(destination: Path, allowed: Path): JsonObject {
        fixturePath(destination, allowed)
        require(Files.isDirectory(destination, NOFOLLOW_LINKS) && Files.getPosixFilePermissions(destination) == privateDirectory.value()) { "Copy workspace must be private" }
        val manifest = destination.resolve("manifest.json")
        val complete = destination.resolve("COMPLETE")
        require(Files.isRegularFile(manifest, NOFOLLOW_LINKS) && Files.size(manifest) <= 64 * 1024 * 1024 &&
            Files.isRegularFile(complete, NOFOLLOW_LINKS) && Files.size(complete) == 65L) { "Copy is incomplete" }
        require(Files.readString(complete) == digest(manifest) + "\n") { "Copy completion hash mismatch" }
        val report = Json.parseToJsonElement(Files.readString(manifest)).jsonObject
        require(report.getValue("version").jsonPrimitive.int == 1 && report.getValue("executionAllowed") == JsonPrimitive(false) &&
            report.getValue("engineValidated") == JsonPrimitive(false)) { "Unsupported copy manifest" }
        val recovered = report.getValue("choices").jsonArray.isNotEmpty()
        val count = report.getValue("payloadRoots").jsonArray.size
        require(count in 0..16)
        val names = mutableSetOf("original")
        if (recovered) names += "candidate"
        names += (0 until count).map { "payloads/$it" }
        val copies = report.getValue("copyInventories").jsonObject
        require(copies.keys == names) { "Incomplete copy inventories" }
        val top = mutableSetOf(QUARANTINE, "STARTED", "COMPLETE", "manifest.json", "original")
        if (recovered) top += "candidate"
        if (count > 0) {
            top += "payloads"
            RuntimePolicy.noSymlinkAncestors(destination.resolve("payloads"))
            Files.list(destination.resolve("payloads")).use { children ->
                require(children.map { it.fileName.toString() }.toList().toSet() == (0 until count).map { it.toString() }.toSet()) { "Unexpected payload copy" }
            }
        }
        Files.list(destination).use { children ->
            val entries = children.toList()
            require(entries.map { it.fileName.toString() }.toSet() == top) { "Unexpected workspace entry" }
            entries.forEach { path ->
                require(!Files.isSymbolicLink(path))
                val permissions = if (Files.isDirectory(path, NOFOLLOW_LINKS)) privateDirectory.value() else privateFile.value()
                require(Files.getPosixFilePermissions(path, NOFOLLOW_LINKS) == permissions) { "Copy permissions are no longer private" }
            }
        }
        require(Files.isRegularFile(destination.resolve(QUARANTINE), NOFOLLOW_LINKS) && Files.isRegularFile(destination.resolve("STARTED"), NOFOLLOW_LINKS))
        for (name in names) {
            val path = destination.resolve(name)
            RuntimePolicy.noSymlinkAncestors(path)
            require(Files.isDirectory(path, NOFOLLOW_LINKS))
            require(inventory(inspect(path, false, privateCopy = true)) == copies.getValue(name)) { "Copy inventory mismatch" }
            if (name in setOf("original", "candidate")) require(Files.isRegularFile(path.resolve(QUARANTINE), NOFOLLOW_LINKS)) { "Missing profile quarantine" }
        }
        return report
    }

    /** Create-only. Any post-creation failure retains its entire owned incomplete workspace.
     * No automatic retry, cleanup, transaction promotion, path rewriting, engine boot or IPC.
     * [checkpoint] is a synchronous fault-injection seam; production CLI supplies none.
     */
    fun create(request: Request, allowed: Path, checkpoint: (String) -> Unit = {}): JsonObject {
        fixturePath(request.source, allowed); fixturePath(request.destination, allowed)
        require(Files.isDirectory(request.source, NOFOLLOW_LINKS) && Files.isDirectory(request.destination.parent, NOFOLLOW_LINKS)) { "Fixture directories must already exist" }
        require(!Files.exists(request.destination, NOFOLLOW_LINKS)) { "Never overwrite/reuse a copy destination" }
        require(request.payloadRoots.size <= 16 && request.payloadRoots.distinct().size == request.payloadRoots.size) { "Invalid payload roots" }
        val roots = listOf(request.source) + request.payloadRoots
        roots.forEach { root ->
            fixturePath(root, allowed); require(Files.isDirectory(root, NOFOLLOW_LINKS))
            require(!request.destination.startsWith(root) && !root.startsWith(request.destination)) { "Copy destination overlaps source" }
        }
        roots.forEachIndexed { index, root -> roots.drop(index + 1).forEach { other ->
            require(!root.startsWith(other) && !other.startsWith(root)) { "Overlapping copy roots refused" }
        } }
        requireNotQuarantined(request.source)
        // Existing lock only: a source without an established owner inode is ambiguous.
        // Do not create a lock, change a source document, or acquire on a copied lock inode.
        return ProfileLease.acquireExisting(request.source.resolve(LOCK)).use {
            val trees = roots.mapIndexed { index, root -> inspect(root, index == 0) }
            require(trees.sumOf { tree -> tree.files.sumOf { it.size } } <= MAX_BYTES) { "Combined fixture byte limit exceeded" }
            require(trees.sumOf { it.files.size + it.directories.size } <= MAX_FILES) { "Combined fixture entry limit exceeded" }
            val choices = validateChoices(trees.first(), request.choices)
            checkpoint("inventoried")
            directory(request.destination)
            try {
                write(request.destination.resolve(QUARANTINE), "Quarantined copy workspace. NOT approved for execution.\n")
                write(request.destination.resolve("STARTED"), "Incomplete until COMPLETE and inventory verification. Never auto-retry or erase.\n")
                checkpoint("created")
                copyTree(request.source, request.destination.resolve("original"), trees.first(), true)
                checkpoint("original-copied")
                if (choices.isNotEmpty()) copyTree(request.source, request.destination.resolve("candidate"), trees.first(), true, choices)
                if (request.payloadRoots.isNotEmpty()) {
                    directory(request.destination.resolve("payloads"))
                    request.payloadRoots.forEachIndexed { index, root -> copyTree(root, request.destination.resolve("payloads/$index"), trees[index + 1], false) }
                }
                checkpoint("copied")
                require(roots.mapIndexed { index, root -> inspect(root, index == 0) } == trees) { "Source/payload changed during copy; outcome incomplete" }
                val copyInventories = linkedMapOf<String, Tree>()
                fun checkCopy(name: String, original: Tree, profile: Boolean, selections: Map<String, Choice> = emptyMap()) {
                    val copied = inspect(request.destination.resolve(name), false, privateCopy = true)
                    validateCopy(original, copied, profile, selections)
                    copyInventories[name] = copied
                }
                checkCopy("original", trees.first(), true)
                if (choices.isNotEmpty()) checkCopy("candidate", trees.first(), true, choices)
                request.payloadRoots.indices.forEach { index -> checkCopy("payloads/$index", trees[index + 1], false) }
                val report = buildJsonObject {
                    put("version", 1); put("outcome", if (choices.isEmpty()) "quarantined-copy-unvalidated" else "quarantined-recovery-candidate-unvalidated")
                    put("engineValidated", false); put("executionAllowed", false); put("externalPayloadsComplete", false)
                    put("source", request.source.toString()); put("excludedProfileLock", LOCK)
                    put("payloadRoots", JsonArray(request.payloadRoots.map { JsonPrimitive(it.toString()) }))
                    put("choices", JsonArray(request.choices.map { c -> buildJsonObject {
                        put("target", c.target); put("use", if (c.useTemporary) "temporary" else "committed")
                        put("committedSha256", c.committedSha256?.let(::JsonPrimitive) ?: JsonNull); put("temporarySha256", c.temporarySha256)
                    } }))
                    put("sourceInventories", JsonArray(trees.map(::inventory)))
                    put("copyInventories", JsonObject(copyInventories.mapValues { (_, tree) -> inventory(tree) }))
                }
                val manifestText = report.toString() + "\n"
                require(manifestText.toByteArray(Charsets.UTF_8).size <= 64 * 1024 * 1024) { "Copy manifest too large; workspace remains incomplete" }
                write(request.destination.resolve("manifest.json"), manifestText)
                checkpoint("manifest-written")
                write(request.destination.resolve("COMPLETE"), digest(request.destination.resolve("manifest.json")) + "\n")
                report
            } catch (e: Exception) {
                // Never echo document bytes, credentials or nested exception messages.
                throw IllegalStateException("Copy/recovery outcome incomplete; retain workspace and inspect before any explicit new attempt", e)
            }
        }
    }
}
