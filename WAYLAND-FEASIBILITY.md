# Native Wayland feasibility probe

Date: 2026-10-15. Checkout: `/home/alasady/Code/ab-download-manager-plasma`,
branch `local/plasma-wayland-1.11.0`.

## Conclusion

**Native Wayland is feasible without rewriting the download engine or UI, but it is not a
runtime-option-only migration with this app's current Compose window backend.**

An isolated smoke process using ABDM's exact packaged libraries and its actual
`CustomWindow` wrapper rendered and completed basic lifecycle tests under JBR's native
Wayland toolkit after a scratch-only Compose rendering override. This was NOT the full
ABDM application: its entrypoint, download engine, data stores, tray, browser integration,
and single-instance/integration server were never started.

No production Kotlin, Gradle dependencies, launcher, installed release, or KWin companion
was changed for this investigation. All runtime downloads, source copies, compilation,
logs, and test home/config/data directories are under the ignored `build/wayland-probe/`.
This report is the only new non-ignored file. Nothing was staged or committed.

## Runtime and isolation

- Tested official JetBrains Runtime `25.0.4.1+1-b635.72`, downloaded from the URL in its
  [official release](https://github.com/JetBrains/JetBrainsRuntime/releases/tag/jbr-release-25.0.4.1b635.72).
- Archive: `https://cache-redirector.jetbrains.com/intellij-jbr/jbr-25.0.4.1-linux-x64-b635.72.tar.gz`.
- Its SHA-512 was verified against the publisher's `.checksum` before extraction/execution:
  `b7cff4d94f5e8240024834d966451c15275b9d5ed01b8a806b0d4ef84248abb53b0b58beb40a8518ab1d2bba8d36878e04424e2251e3b0be87098fed219cc47d`.
- Existing Arch OpenJDK 25 and 27 both have the X11 AWT library but no Wayland AWT library.
  The downloaded JBR includes `libawt_wlawt.so` as well as its X11 backend.
- Same packaged Compose `1.12.1`, Skiko `0.150.1`, and Kotlin `2.4.20` as the working app.
  Scratch code was compiled for Java 25 using cached Kotlin/Compose compilers; no root
  project build configuration was modified.
- Native processes explicitly selected `-Dawt.toolkit.name=WLToolkit` and had `DISPLAY`
  removed from their environment. They printed `toolkit=sun.awt.wl.WLToolkit` and
  `DISPLAY=null`. `WAYLAND_DEBUG=1` recorded actual `xdg_toplevel`/`wl_surface` traffic.
  Successful cases attached buffers and committed surfaces to KWin. This is not an
  XWayland window merely running in a Wayland session.
- Test processes used scratch `HOME`, `user.home`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, and
  `XDG_CACHE_HOME`. They inherited only the existing display/session access, did not
  inject pointer/keyboard events globally, and exited on their own (with a process-local
  timeout safeguard in the runner).

## Test results

Renderer requested: `SKIKO_RENDER_API=SOFTWARE`, consistent with the normal launcher's
software-rendering preference. These tests do not establish hardware acceleration support.

| Case | Result |
| --- | --- |
| Plain Swing/AWT on native WLToolkit | PASS: window opened, painted, resized, and disposed; exit 0. |
| Stock `ComposeWindow` on native WLToolkit | FAIL: `IllegalStateException: Can't lock DrawingSurface`; exit 2. |
| Stock `ComposeWindow` plus `-Dcompose.swing.render.on.graphics=true` | Same failure; this flag does not apply to `ComposeWindow`. |
| `JFrame` + `ComposePanel(RenderSettings.SwingGraphics())` on native WLToolkit | PASS: Compose drawing/state updates, popup exercise, resize, maximize, restore, disposal; exit 0. |
| Stock `ComposeWindow` on the same JBR using XToolkit/XWayland | PASS: the same content/lifecycle sequence; exit 0. |
| `ComposeWindow` on native WLToolkit with scratch rendering overlay | PASS: same content/lifecycle sequence; exit 0. |
| ABDM's actual `CustomWindow` on native WLToolkit with overlay | PASS: 20 draw callbacks, 5 timed state updates, resize, vetoed WM-close callback, maximize/restore, second accepted WM-close callback, and composition disposal; exit 0. |

The draw counts are callbacks, not benchmark FPS or a visual correctness measurement.
The popup was exercised by timed Compose state changes, not clicked by a real user.
The CustomWindow maximize/restore requests came through the v2 window state API, not a
physical titlebar button click. The close requests were AWT `WINDOW_CLOSING` events
explicitly dispatched to the test window. This checks the real callback/disposal path,
not native compositor-originated close input or the appearance of the close animation.

Last CustomWindow log included:

```text
toolkit=sun.awt.wl.WLToolkit
DISPLAY=null
composeWindowPanelSource=file:.../build/wayland-probe/overlay-classes/
customWindowOpened=true
jbrDecorations=false
jbrWindowMove=true
ewmhMove=false
customNativeHandle=0
customCloseRequest=1
customMaximizedAfterVeto=6
customRestored=0
customCloseRequest=2
CUSTOM_SMOKE_COMPLETE draws=20 updates=5 closes=2
```

`Frame.MAXIMIZED_BOTH` is 6 and normal state is 0. JBR's move API being available is
promising, but does not prove a real drag works or preserves the confirmed animation.

## Why the stock window fails, and what the overlay changes

The stock native failure occurs before any ABDM window code is needed:

```text
org.jetbrains.skiko.DrawingSurface.lock
org.jetbrains.skiko.AWTKt.useDrawingSurfaceInfo
org.jetbrains.skiko.HardwareLayer.init
org.jetbrains.skiko.SkiaLayer.init
androidx.compose.ui.scene.skia.SurfaceSkiaLayerComponent...
```

The exact Compose 1.12.1 sources were downloaded from Maven Central to
`build/wayland-probe/ui-sources.jar`. Relevant extracted source references:

- `sources/desktopMain/androidx/compose/ui/ComposeFeatureFlags.desktop.kt:60-76` explicitly
  documents that `compose.swing.render.on.graphics` affects ComposePanel, not
  ComposeWindow/ComposeDialog, and that the Swing path adds a per-redraw copy.
- `sources/desktopMain/androidx/compose/ui/awt/ComposeWindowPanel.desktop.kt:59-75` hardcodes
  `RenderSettings.SkiaSurface()`; its comment says Swing graphics is not intended here.
- `sources/desktopMain/androidx/compose/ui/scene/skia/SwingSkiaLayerComponent.desktop.kt:112-121`
  reports fullscreen as false and its native window handle as 0. These are real limitations
  to review before a port, not evidence that the current X11 helpers can be reused unchanged.

For the experiment ONLY, a copy of `ComposeWindowPanel.desktop.kt` changed:

```diff
-        renderSettings = RenderSettings.SkiaSurface(),
+        renderSettings = RenderSettings.SwingGraphics(),
```

That one source file was recompiled against the exact UI jar with Kotlin friend access
and required experimental/internal API opt-ins. Its classes were prepended to the smoke
process's classpath. The original packaged jar was never rewritten. The logged class
code-source confirms which runs used the overlay.

This proves a narrow existing rendering path can draw the UI natively. It is **not** a
supported upstream switch, a completed renderer port, or justification to add a hidden
classpath override to the normal installation. Prefer a properly reviewed upstream or
version-pinned Compose change exposing a supported window rendering choice if continuing.

## What remains before a useful experimental ABDM build

1. **Rendering integration:** turn the unsupported source overlay into a maintainable,
   explicit opt-in implementation, covering dialogs/layers as well as the main window.
   Test rendering correctness and CPU/frame-copy costs at realistic window sizes.
2. **Backend detection:** distinguish XToolkit from WLToolkit, rather than treating every
   Linux window handle as an XID. Explicitly disable X11 EWMH queries/unmap/move helpers on
   native Wayland even if `DISPLAY` also exists. The smoke intentionally removed DISPLAY;
   an ordinary desktop launch has both display variables. Do not rely on the current
   Swing backend returning handle 0 as the permanent safety mechanism.
3. **Window management:** test real titlebar drags, maximize and restore animations,
   native WM-fullscreen transitions, resizers, canceled/deferred closes, and focus/activation.
   Retain X11 fixes for the XWayland fallback; don't remove the intentional companion.
4. **Scaling and geometry:** reassess the staged XWayland-oriented global scale correction
   and absolute position restoration for a compositor-controlled native backend. The test
   JBR reported default screen transforms 1.0 and 2.0 for this host's two monitors; do not
   interpret the successful drawing test as proof of correct fractional/mixed-DPI behavior.
5. **Desktop integration:** validate real keyboard/text entry/IME, clipboard, external
   drag/drop, file dialogs, multiple windows, notifications/tray, launch/focus behavior,
   and browser integration in a fully isolated app instance. None is established here.
6. **Keep the normal installation unchanged** until the experimental build passes those
   checks. A working proof of concept does not justify replacing the approved close/drag
   behavior with an unvalidated native backend.

## Evidence and rerunning the retained scratch tests

All paths below are relative to this checkout. `build/` is ignored and can be deleted by
Gradle clean; this report remains outside it.

- `build/wayland-probe/AwtProbe.java`: initial Java-only AWT/Compose surface comparison.
- `build/wayland-probe/src/main/kotlin/WaylandSmoke.kt`: standalone Compose and actual
  ABDM CustomWindow smoke mains; neither calls the app entrypoint.
- `build/wayland-probe/overlay-src/ComposeWindowPanel.desktop.kt`: full licensed upstream
  source copy with the scratch-only rendering substitution.
- `build/wayland-probe/compile-smoke.py`: compiles with already-cached Kotlin 2.4.20 and
  the existing packaged libraries, without changing the root Gradle project.
- `build/wayland-probe/run-smoke.py`: named native/control cases, isolated environment,
  logs, loaded-library maps, process-local timeout, and `smoke-results.json` summary.
- `build/wayland-probe/*-software.log`, `*-overlay.log`, `x11-window-control.log`,
  `native-window-swing-flag.log`: actual runtime/Wayland protocol evidence.
- `build/wayland-probe/before-{worktree,index}.diff` and `before-installed.json`: preservation
  snapshots; the installed snapshot records only release/PID and companion hashes.

With the retained scratch runtime, cached compilers, and packaged app artifacts present:

```sh
cd /home/alasady/Code/ab-download-manager-plasma
PYTHONDONTWRITEBYTECODE=1 python3 build/wayland-probe/compile-smoke.py --overlay
PYTHONDONTWRITEBYTECODE=1 python3 build/wayland-probe/compile-smoke.py
PYTHONDONTWRITEBYTECODE=1 python3 build/wayland-probe/run-smoke.py
# Or just the successful native app-window-wrapper proof:
PYTHONDONTWRITEBYTECODE=1 python3 build/wayland-probe/run-smoke.py native-custom-overlay
```

The matrix intentionally includes expected-failing cases; inspect their exit statuses in
`smoke-results.json`, not just the runner's exit code. Do not use these commands as an
installer or point the normal launcher at the overlay/runtime.

An initial standalone Gradle scratch build could not resolve its plugin marker offline;
compilation was then done directly with the already-cached compiler. That build attempt
did not change the root project or dependencies and is not a platform finding.

## Preserved normal state

- Normal app remained PID `619126`, release `1.11.0+plasma.2-20261015-022633`.
- Normal runtime/launcher was not replaced or restarted by these tests.
- Owned KWin companion files remain byte-identical to the pre-probe hash snapshot.
- Existing tracked worktree diff remains byte-identical to the pre-probe snapshot.
- Staged diff remains byte-identical, SHA-256:
  `926f0ea202579a70673e43841f013e14cf91172a26057a562928dab28b7093e8`.
- No staged plasma.1 work, working close/drag implementation, or app settings were edited.
