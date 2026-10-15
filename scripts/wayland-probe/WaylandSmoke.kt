@file:OptIn(androidx.compose.ui.ExperimentalComposeUiApi::class)

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.material.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.awt.ComposePanel
import androidx.compose.ui.awt.ComposeWindow
import androidx.compose.ui.awt.RenderSettings
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.delay
import java.awt.Frame
import java.awt.Toolkit
import java.awt.event.WindowAdapter
import java.awt.event.WindowEvent
import java.util.concurrent.atomic.AtomicInteger
import javax.swing.JFrame
import javax.swing.SwingUtilities
import javax.swing.Timer
import javax.swing.WindowConstants
import kotlin.system.exitProcess

/** Standalone rendering test. Does NOT boot ABDM, touch its data, or load its X11 helpers. */
fun main(args: Array<String>) {
    val mode = args.firstOrNull() ?: "panel"
    Thread.setDefaultUncaughtExceptionHandler { thread, error ->
        System.err.println("SMOKE_FAILED thread=${thread.name}")
        error.printStackTrace()
        exitProcess(2)
    }
    println("runtime=${System.getProperty("java.runtime.version")}")
    println("toolkit=${Toolkit.getDefaultToolkit().javaClass.name}")
    println("DISPLAY=${System.getenv("DISPLAY")}")
    println("mode=$mode")
    println("composeWindowPanelSource=${Class.forName("androidx.compose.ui.awt.ComposeWindowPanel").protectionDomain.codeSource.location}")
    java.awt.GraphicsEnvironment.getLocalGraphicsEnvironment().screenDevices.forEach { device ->
        println("screen=${device.iDstring} bounds=${device.defaultConfiguration.bounds} scale=${device.defaultConfiguration.defaultTransform.scaleX}")
    }
    val draws = AtomicInteger()
    val updates = AtomicInteger()
    if (mode == "custom") {
        customWindowSmoke(draws, updates)
        return
    }
    SwingUtilities.invokeLater {
        val frame: JFrame
        if (mode == "window") {
            frame = ComposeWindow().apply {
                setContent { SmokeContent(draws, updates) }
            }
        } else {
            frame = JFrame()
            val panel = ComposePanel(renderSettings = RenderSettings.SwingGraphics())
            panel.setContent { SmokeContent(draws, updates) }
            frame.contentPane = panel
        }
        frame.title = "ABDM isolated Wayland Compose probe: $mode"
        frame.defaultCloseOperation = WindowConstants.DISPOSE_ON_CLOSE
        frame.addWindowListener(object : WindowAdapter() {
            override fun windowOpened(e: WindowEvent) {
                println("windowOpened=true")
            }
        })
        frame.addWindowStateListener {
            println("windowState=${it.newState}")
        }
        frame.setSize(700, 460)
        frame.isVisible = true
        fun later(ms: Int, action: () -> Unit) {
            Timer(ms) { action() }.apply { isRepeats = false; start() }
        }
        later(1000) {
            frame.setSize(760, 500)
            println("resizeRequested=true")
        }
        later(1800) {
            frame.extendedState = Frame.MAXIMIZED_BOTH
            println("maximizeRequested=true")
        }
        later(2800) {
            println("maximizedObserved=${frame.extendedState}")
            frame.extendedState = Frame.NORMAL
            println("restoreRequested=true")
        }
        later(3800) {
            println("restoredObserved=${frame.extendedState}")
            frame.dispose()
            println("SMOKE_COMPLETE draws=${draws.get()} updates=${updates.get()}")
            exitProcess(if (draws.get() > 2 && updates.get() >= 3) 0 else 3)
        }
    }
}

private fun customWindowSmoke(draws: AtomicInteger, updates: AtomicInteger) =
    androidx.compose.ui.window.application {
        var open by remember { mutableStateOf(true) }
        var closeRequests by remember { mutableStateOf(0) }
        val state = androidx.compose.ui.window.v2.rememberWindowState()
        CompositionLocalProvider(
            com.abdownloadmanager.shared.util.ui.LocalTitleBarDirection provides
                androidx.compose.ui.unit.LayoutDirection.Ltr
        ) {
        com.abdownloadmanager.shared.ui.theme.ABDownloaderTheme(
            com.abdownloadmanager.shared.ui.theme.DefaultThemes.light
        ) {
            if (open) {
                com.abdownloadmanager.desktop.window.custom.CustomWindow(
                    state = state,
                    onCloseRequest = {
                        closeRequests++
                        println("customCloseRequest=$closeRequests")
                        if (closeRequests >= 2) open = false
                    },
                    windowController = com.abdownloadmanager.desktop.window.custom.rememberWindowController(
                        title = "ABDM isolated native Wayland CustomWindow probe",
                        icon = androidx.compose.ui.graphics.painter.ColorPainter(Color(0xff195582))
                    ),
                ) {
                    SmokeContent(draws, updates)
                    LaunchedEffect(window) {
                        println("customWindowOpened=true")
                        println("jbrDecorations=${com.jetbrains.JBR.isWindowDecorationsSupported()}")
                        println("jbrWindowMove=${com.jetbrains.JBR.getWindowMove() != null}")
                        println("ewmhMove=${com.abdownloadmanager.desktop.window.linux.canEwmhMove()}")
                        println("customNativeHandle=${runCatching { window.windowHandle }.getOrNull()}")
                        delay(1000)
                        window.setSize(760, 500)
                        println("customResizeRequested=true")
                        // Exercise the real CustomWindow WM-close callback, vetoing once.
                        window.dispatchEvent(WindowEvent(window, WindowEvent.WINDOW_CLOSING))
                        delay(400)
                        state.requestPlacement(androidx.compose.ui.window.WindowPlacement.Maximized)
                        delay(900)
                        check(window.extendedState and Frame.MAXIMIZED_BOTH == Frame.MAXIMIZED_BOTH)
                        println("customMaximizedAfterVeto=${window.extendedState}")
                        state.requestPlacement(androidx.compose.ui.window.WindowPlacement.Floating)
                        delay(900)
                        check(window.extendedState and Frame.MAXIMIZED_BOTH == 0)
                        println("customRestored=${window.extendedState}")
                        window.dispatchEvent(WindowEvent(window, WindowEvent.WINDOW_CLOSING))
                    }
                }
            }
        }
        }
        LaunchedEffect(open) {
            if (!open) {
                delay(200)
                println("CUSTOM_SMOKE_COMPLETE draws=${draws.get()} updates=${updates.get()} closes=$closeRequests")
                check(draws.get() > 2 && updates.get() >= 3 && closeRequests == 2)
                exitApplication()
            }
        }
    }

@Composable
private fun SmokeContent(draws: AtomicInteger, updates: AtomicInteger) {
    var text by remember { mutableStateOf("Native Wayland Compose feasibility test") }
    var tick by remember { mutableStateOf(0) }
    var menu by remember { mutableStateOf(false) }
    val density = androidx.compose.ui.platform.LocalDensity.current
    LaunchedEffect(Unit) {
        println("composeDensity=${density.density} fontScale=${density.fontScale}")
        repeat(5) {
            delay(250)
            tick++
            updates.incrementAndGet()
            menu = tick == 2 || tick == 3
        }
    }
    MaterialTheme {
        Column(
            Modifier.fillMaxSize().background(Color(0xffedf4fa))
                .drawWithContent { drawContent(); draws.incrementAndGet() }
                .padding(24.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            Text("ABDM library stack — isolated smoke test", style = MaterialTheme.typography.h6)
            Text("Toolkit: ${Toolkit.getDefaultToolkit().javaClass.simpleName}; update $tick")
            TextField(value = text, onValueChange = { text = it }, modifier = Modifier.fillMaxWidth())
            Box {
                Button(onClick = { menu = !menu }) { Text("Popup menu") }
                DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
                    DropdownMenuItem(onClick = { menu = false }) { Text("Compose popup rendered") }
                }
            }
            LinearProgressIndicator(progress = tick / 5f, modifier = Modifier.fillMaxWidth())
        }
    }
}
