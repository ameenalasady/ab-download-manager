package com.abdownloadmanager.service

import kotlinx.coroutines.runBlocking
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.attribute.PosixFilePermissions
import kotlin.concurrent.thread

/** Two separate, explicit policies; never auto-discover or convert a production profile. */
fun main(args: Array<String>) {
    require(args.size == 2 && args[0] in listOf("--isolated-profile", "--runtime-policy")) {
        "Use --isolated-profile <fixture> OR --runtime-policy <private-policy.json>"
    }
    val runtime = if (args[0] == "--runtime-policy") RuntimePolicy.load(Path.of(args[1]).toAbsolutePath().normalize()) else {
        val checkout = Path.of("").toAbsolutePath().normalize()
        check(Files.isRegularFile(checkout.resolve("settings.gradle.kts"))) { "Launch isolated mode from repository root" }
        val allowed = checkout.resolve("build/qt-service")
        val profile = Path.of(args[1]).toAbsolutePath().normalize()
        require(profile.startsWith(allowed) && profile != allowed) { "Only build/qt-service fixture profiles are allowed" }
        RuntimePolicy.noSymlinkAncestors(profile)
        Files.createDirectories(profile)
        Files.walk(profile).use { paths -> require(paths.noneMatch { Files.isSymbolicLink(it) }) { "Symlinks in fixture profile refused" } }
        require(!Files.exists(profile.resolve(".abdm-desktop-integration.properties")))
        Files.setPosixFilePermissions(profile, PosixFilePermissions.fromString("rwx------"))
        RuntimePolicy.isolated(profile)
    }
    // Only the explicit-runtime launcher provides how to open the frontend for browser requests.
    val launcher = FrontendLauncher.fromEnvironment(if (runtime.isolated) null else System.getenv("ABDM_QT_FRONTEND_LAUNCH"))
    val activityCommand = if (runtime.isolated) null else MonitoredSystemActivity.commandFrom(System.getenv("ABDM_QT_ACTIVITY_MONITOR"))
    val lifecycle = ServiceLifecycle(runtime.profile, runtime, launcher = launcher,
        notifier = { if (runtime.isolated) NoDesktopNotifier else FreedesktopNotifier() }, activityCommand = activityCommand)
    var ipc: PrivateIpcServer? = null
    // Start/constructor failures use the same joined cleanup as normal exit.
    val cleanup = {
        ipc?.close() // A failed IPC drain must NOT release the writer lease.
        runBlocking { lifecycle.stop() }
    }
    var hook: Thread? = null
    try {
        val engine = runBlocking { lifecycle.start() }
        ipc = PrivateIpcServer(runtime.profile, engine)
        hook = thread(start = false, name = "service-shutdown") { cleanup() }
        Runtime.getRuntime().addShutdownHook(hook)
        ipc.serve()
    } finally {
        val registered = hook
        if (registered == null || runCatching { Runtime.getRuntime().removeShutdownHook(registered) }.getOrDefault(false)) cleanup()
    }
}
