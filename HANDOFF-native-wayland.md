# Handoff: native Wayland implementation

**Read this file first.** It supersedes the old cleanup/fullscreen handoff task and Git
state instructions. Then read `WAYLAND-FEASIBILITY.md`, `scripts/wayland-probe/README.md`,
and the current-state summary in `PLASMA-LOCAL.md`. Earlier fullscreen/cleanup documents
remain historical evidence, not instructions to reintroduce their superseded mitigations.

## 1. User request and repository state

The user approved the working Plasma build and cleanup, asked whether native Wayland was
feasible, authorized an isolated experiment, and then asked to fork/push the current work
and hand off native implementation on a new branch. Fork/push/bootstrap are now prepared;
the next task is **developing an isolated opt-in native Wayland implementation**.

- Worktree: `/home/alasady/Code/ab-download-manager-plasma`.
- DO NOT use `/home/alasady/Code/ab-download-manager`: that checkout is stale.
- Fork: <https://github.com/ameenalasady/ab-download-manager>.
- Upstream: <https://github.com/amir1376/ab-download-manager>.
- Implementation branch: **`native-wayland`**.
- Known-good fallback branch: **`local/plasma-wayland-1.11.0`**.
- Stable fallback tip: **`10a8d58dcd4e354b57ce73fed86052e73dca7cba`**.
- Base upstream v1.11.0: `5b2cffe4765bc66f5ab82f36bd878714bdea3fd7`.

The user explicitly authorized committing/pushing for this fork handoff. The formerly
staged plasma.1 work is no longer waiting in the index: it was committed separately,
byte-for-byte identical to its original staged patch:

1. `a8cb5aa7` — plasma.1 port/scaling/integration work. Original patch SHA-256:
   `926f0ea202579a70673e43841f013e14cf91172a26057a562928dab28b7093e8`.
2. `10a8d58d` — current plasma.2 close/drag/fullscreen fixes, intentional KWin companion,
   cleanup, tests, install docs, and investigation/feasibility records.

`native-wayland` starts at that stable tip and adds only this handoff, reproducible probe
sources, and a pointer from the feasibility report. There is **no production native
backend implementation yet**, no production dependency override, and no installed native
build. Keep fallback behavior intact; do not rewrite these baseline commits or force-push.

In this original local worktree:

```text
fork   = https://github.com/ameenalasady/ab-download-manager.git
origin = https://github.com/amir1376/ab-download-manager.git
```

Local `remote.pushDefault` is `fork`; the branches track the fork. Push only to the fork,
not the upstream origin. A fresh clone of the fork may call its fork remote `origin`, so
check `git remote -v`, branch, status, and current user changes rather than assuming names.
Do not merge an experimental native backend into the stable fallback branch by default.

## 2. Current installed state: leave it alone

Normal desktop use remains the approved **XWayland** build:

- Version: `1.11.0+plasma.2`.
- Release: `~/.local/opt/ab-download-manager-plasma/releases/1.11.0+plasma.2-20261015-022633`.
- `current` points there; PID at handoff: `619126` (re-check, not a permanent identity).
- KDE entry: `~/.local/share/applications/com.abdownloadmanager.desktop`.
- Main wrapper: `~/.local/bin/ABDownloadManager` -> `current/bin/ABDownloadManager`.
- Backup from cleanup install: `~/.local/state/abdm-backups/install-20261015-022630/`.
- User data stays in its normal locations; no test has booted ABDM against those stores.
- Owned KWin maximize companion remains installed, unchanged by the Wayland experiments.

Do not replace/restart that installation, rewrite its desktop entry, modify its data,
remove its compositor companion, or run the normal installer for experiments. Use separate
experimental data/config/cache directories and an unmistakable test title/identity.
Get user approval before installing or promoting a native build.

## 3. What the feasibility experiment established

Runtime: official **JBR `25.0.4.1+1-b635.72`**, verified against the publisher's SHA-512.
Existing Arch OpenJDK 25/27 on this host do not contain the native Wayland AWT backend.
The app's Java 25 classfiles run on this JBR 25; do not substitute JBR 21 blindly.

Same exact packaged libraries: Compose `1.12.1`, Skiko `0.150.1`, Kotlin `2.4.20`.

Measured results:

- Native JBR Swing window works.
- Stock ComposeWindow fails under WLToolkit with **`Can't lock DrawingSurface`** inside
  Skiko's AWT/HardwareLayer initialization, before ABDM-specific window logic is required.
- `-Dcompose.swing.render.on.graphics=true` does NOT fix ComposeWindow; that flag affects
  ComposePanel, not standalone ComposeWindow/ComposeDialog.
- A JFrame embedding `ComposePanel(RenderSettings.SwingGraphics())` works natively.
- The same stock ComposeWindow works under the same JBR's XToolkit (XWayland control).
- A scratch-only source overlay changing the internal ComposeWindowPanel rendering
  setting to `RenderSettings.SwingGraphics()` also makes ComposeWindow work natively.
- With that overlay, ABDM's **actual compiled `CustomWindow` wrapper** rendered and
  completed resize, maximize, restore, first-vetoed close request, second-accepted close
  request, and disposal. This was a standalone smoke main, NOT the complete app.

Successful native runs had **DISPLAY unset**, printed `sun.awt.wl.WLToolkit`, loaded
`libawt_wlawt.so`, and recorded real Wayland buffer attach/commit traffic. They could not
silently fall back to XWayland.

The actual CustomWindow smoke reported:

```text
jbrDecorations=false
jbrWindowMove=true
ewmhMove=false
customNativeHandle=0
customMaximizedAfterVeto=6
customRestored=0
CUSTOM_SMOKE_COMPLETE ... closes=2
```

Available JBR move API is promising, but **real dragging/input and animation appearance
were not tested**. Maximize/restore used v2 state requests; close requests were dispatched
AWT WINDOW_CLOSING events. Timed Compose state changes exercised drawing/popups, not real
keyboard/IME/clipboard input. No GPU performance, fractional/mixed-DPI correctness,
full-app behavior, or desktop integration claims have been established.

## 4. Reproduce before changing implementation

The original ignored `build/wayland-probe/` still contains the downloaded runtime, original
source archive, compiled probes, maps, and protocol logs. It does not travel with Git and
can disappear on clean. The source harness is now retained in:

- `scripts/wayland-probe/README.md` — preparation, checksums, exact commands and caveats.
- `scripts/wayland-probe/WaylandSmoke.kt` — native/control/panel/real-CustomWindow modes.
- `scripts/wayland-probe/compile.py` — cached compiler + exact packaged libraries;
  `--overlay` extracts/pins upstream source and produces scratch-only override classes.
- `scripts/wayland-probe/run.py` — isolated bounded cases and expected-result matrix.

The new tracked harness reproduces the original scratch experiment, not a root-project
build dependency or launch hook. The overlay preserves upstream source licensing in the
extracted file and never rewrites original packaged jars.

Run from the real repository root after reading that README:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 scripts/wayland-probe/compile.py --overlay
PYTHONDONTWRITEBYTECODE=1 python3 scripts/wayland-probe/compile.py
PYTHONDONTWRITEBYTECODE=1 python3 scripts/wayland-probe/run.py
```

The matrix contains two expected native failures. The runner exits nonzero on outcomes
that differ from the documented baseline; inspect the per-case logs and expectations,
not just the overall command exit. Do not package the unsupported overlay as a hidden fix.

## 5. Suggested implementation sequence

1. **Explicit backend detection/routing before any full native app launch.** Current
   X11Ewmh/LinuxWindowState code checks Linux, not toolkit kind. On an ordinary native
   desktop both DISPLAY and WAYLAND_DISPLAY exist. Native code must never treat a
   Wayland handle as an XID, even if XOpenDisplay would succeed. The scratch Swing path's
   handle 0 is not a sufficient permanent safety mechanism. Keep X11 helpers for XToolkit.
2. **Maintainable native rendering selection.** Investigate an upstream-supported option
   or a clearly pinned/reviewable Compose change instead of a hidden classpath trick.
   Compose's source explicitly says the tested Swing rendering mode is not intended for
   standalone windows. Review dialog/layer/focus/transparency/fullscreen assumptions;
   SwingSkiaLayerComponent currently reports fullscreen false and native handle 0.
3. **Runtime/packaging strategy.** Keep XWayland fallback and allow explicit native opt-in.
   A native distributable must actually bundle a Wayland-capable runtime. Compiling with
   regular Arch Java 25 or adding WLToolkit to an X11-only jlinked image will not do that.
   The proof uses an external JBR runtime; it has not produced a native app image. If SDK
   tools/jlink are needed, acquire a matching trusted JBR SDK rather than assuming the
   downloaded runtime archive includes every packaging tool.
4. **Standalone window-level checks first.** Extend the retained probe with native-safe
   routing and actual supported gestures/input, before booting the complete application.
   Preserve vetoed/deferred closes, resize/maximize/restore, and other-platform behavior.
5. **Scaling/positioning.** Review `SystemUiScale`, `SystemFontScale`, `ScreenGeometry`,
   persisted bounds, and the staged global XWayland scale compensation. Wayland controls
   absolute placement; the native runtime reported transforms 1.0 and 2.0 for this host's
   monitors. That is not proof of correct 125%/mixed-DPI behavior. Don't stack old scale
   compensation blindly on top of native runtime scaling.
6. **Full app isolation.** Trace all boot paths before launching: user.home and XDG paths
   alone may not isolate fixed ports/single-instance behavior, autostart, tray, browser
   manifests, or integration broadcasts. Use independent ports/identity/settings and
   disable production integration actions. Never use the normal launcher/installer to
   get an experimental process running.
7. **Acceptance and performance.** Test native keyboard/IME, real titlebar drag and
   restore animation, maximize/button/double-click, fullscreen exit+drag, floating and
   enlarged close effects, popup/dialog focus, clipboard/external drag/drop, multi-monitor
   DPI changes, tray/notifications, activation, persistence, and a real large-window CPU
   rendering baseline. The Swing path adds a frame copy; callback counts are not FPS.

Keep changes opt-in and reviewable. Do not invent delays/fake app-size rounding to hide
compositor behavior. The native proof means a broad UI/engine rewrite is not a prerequisite,
but does not establish that a one-line production patch is safe.

## 6. Confirmed fallback behavior that must survive

Prefix: `desktop/app-utils/src/main/kotlin/com/abdownloadmanager/desktop/window/`.

- `custom/CustomWindow.kt`: the native window reference is published through SideEffect
  into WindowDisposalHandle. An OUTER DisposableEffect AFTER Window runs before AWT
  disposal. Do not move this preparation into window content or onto a close request.
- `linux/LinuxWindowState.kt` + `linux/X11Ewmh.kt`: for the X11 fallback, native unmap +
  XSync precedes AWT's unwanted clear-maximized-before-unmap restore. Normal close
  animation remains enabled. No KDE skip-close-animation writes should be reintroduced.
- Title-content-only drag hit testing excludes buttons. Native restore-on-drag and the
  deliberate fullscreen threshold are functional, not redundant. Preserve JBR/Windows/macOS
  fallback branches rather than equating this host's backend with the entire project.
- Home/settings persistence avoids fullscreen and native maximized geometry corruption.
- The obsolete close-request latch/callback publishing bridge and unused helpers were
  already removed. Do not resurrect them from old handoff prose.
- Intentional ABDM-only KWin companion: `scripts/plasma-maximize-workaround.py`, installed
  at `~/.local/share/kwin-wayland/effects/maximize/`. It changes the two maximize-effect
  size comparisons with a 1e-6 tolerance only for ABDM. It is NOT temporary instrumentation.
  Ownership/hash/source guards are intentional. Don't remove/overwrite it or assume
  native Wayland automatically fixes the same compositor roundoff issue.

## 7. Regression commands and handoff expectations

Current fallback baseline: seven Kotlin tests + six Python/JavaScript tests, none skipped.
The Kotlin ordering test models lifecycle order headlessly; it is not full GUI coverage.

```sh
cd /home/alasady/Code/ab-download-manager-plasma
JAVA_HOME=/usr/lib/jvm/java-25-openjdk ./gradlew \
  -PlocalAppVersion=1.11.0+plasma.2 :desktop:app-utils:test
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s scripts/tests -p 'test_plasma_*.py' -v
bash -n scripts/install-plasma-local.sh
git diff --check
```

Node is needed for JavaScript behavior coverage; do not call a skipped case a pass.
For artifact creation without installation, the probe README has a build-only command.
The standard installer is for the approved fallback, not experimental native packaging.

Before concluding native implementation work, report what is truly native, which runtime
and renderer were used, how app state/integration were isolated, what is actually tested,
remaining unsupported features, and whether fallback regressions were checked. Don't claim
readiness or replace the approved installation based only on the smoke test.

Publication validation: the seven Kotlin and six Python/JavaScript tests passed (none
skipped). The retained probe compiler and runner were exercised again: four successful
cases and the two documented DrawingSurface failures matched expectations. The native
branch bootstrap contains only docs and probe sources; runtime downloads, binaries, logs,
and test data remain ignored. The installed app and owned KWin companion were not changed.

## Suggested next-session request

> Work only in ~/Code/ab-download-manager-plasma on branch native-wayland. Read
> HANDOFF-native-wayland.md first, then WAYLAND-FEASIBILITY.md and the probe README.
> Develop an isolated, explicit-opt-in native Wayland implementation based on the proven
> JBR25/Compose rendering experiment. Keep the known-good XWayland fallback and the
> intentional KWin companion intact. Start with backend-safe routing and reproducible
> rendering/window tests. Do not use the stale checkout or replace the installed app.
> Report unsupported behavior and test evidence before proposing installation.
