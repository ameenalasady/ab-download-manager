# Handoff: native Wayland implementation

**Read this file first.** It supersedes the old cleanup/fullscreen handoff task and Git
state instructions. Then read `WAYLAND-FEASIBILITY.md`, `scripts/wayland-probe/README.md`,
and the current-state summary in `PLASMA-LOCAL.md`. Earlier fullscreen/cleanup documents
remain historical evidence, not instructions to reintroduce their superseded mitigations.

**Implementation update:** read [`NATIVE-WAYLAND.md`](NATIVE-WAYLAND.md) and
[`scripts/native-wayland/README.md`](scripts/native-wayland/README.md) for current status.
Milestone 1 added toolkit-safe routing, native scale gating and a pinned opt-in Compose
artifact with a 14-case window matrix. The user then requested the full native app build:
**milestone 2 now packages and tests the real app/engine/stores with a full pinned JBR**, in
an audited isolated profile. Native/XWayland download, persistence, second-launch, scoped
quit and relocated-archive checks pass. The user subsequently **explicitly approved installing
native and replacing the normal desktop entry** for real-life testing. This activation is
now complete; see section 2. The user then explicitly requested **full native use**, including browser capture and other
integrations. Native initially took over browser/HTTP/CLI/autostart/tray routing with
`1.11.0+native.2`; the user subsequently approved the external-app fix update to **native.3**.
The idle old XWayland instance was gracefully stopped. Fallback binaries/data and the KWin
companion are preserved, with integration rollback saved. Implementation, tooling, tests
and documentation are versioned on `native-wayland`; generated artifacts/evidence stay ignored.
Original feasibility results below remain historical baseline evidence.

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

At bootstrap, `native-wayland` started at that stable tip with only this handoff,
reproducible probe sources, and a feasibility-report pointer. Subsequent implementation
is described in `NATIVE-WAYLAND.md`: toolkit-safe routing, opt-in rendering and an isolated
full-app native bundle, with **no normal rendering dependency override**. Following explicit
user approval, that bundle is now separately installed and selected by the desktop entry.
Keep fallback behavior intact; do not rewrite these baseline commits or force-push.

In this original local worktree:

```text
fork   = https://github.com/ameenalasady/ab-download-manager.git
origin = https://github.com/amir1376/ab-download-manager.git
```

Local `remote.pushDefault` is `fork`; the branches track the fork. Push only to the fork,
not the upstream origin. A fresh clone of the fork may call its fork remote `origin`, so
check `git remote -v`, branch, status, and current user changes rather than assuming names.
Do not merge an experimental native backend into the stable fallback branch by default.

## 2. Current installed state: full native integration, fallback preserved

**Latest approved update (2026-10-16): native.3 is installed and running.** Opening media
previously inherited the private HOME/XDG, selecting `/usr/bin/vlc` instead of the user's
`~/Code/vlc-bt-fix/install/bin/vlc` desktop override. Native.3 captures the original desktop
context, carries it through internal browser cold starts and restores it only for external
command helpers on an activated profile. Linux URL opening also uses that scoped handoff.
ABDM's own stores/IPC/JVM remain isolated; the original desktop HOME is policy-checked.
The user explicitly approved installing the fix and gracefully restarting only when idle.
All nine existing native items were completed (persisted and read-only live RPC checks).
PID 730009 exited gracefully; immutable release `ABDownloadManagerNativeWayland-20261016-000710`
is now selected, with real-use PID **751952**, WLToolkit, HTTP 15151 and IPC **34321** at
verification. It loaded all nine existing items. Native settings/history were identical
immediately after installation; later live-session changes (trackDeletedFilesOnDisk enabled
and download history cleared) were detected and retained, NOT overwritten/restored.
**Do not stop/smoke this running user session.** No real media playback was automated.
VLC desktop/defaults, all seven browser manifests, fallback release/current/data and KWin
were preserved (3767 protected file hashes). Native tray/API/host routing still work.
Latest upgrade rollback (native.3 -> fully integrated native.2):
`~/.local/state/abdm-backups/native-integration-20261016-001153-bf4f31/restore-integrations.sh`.
A quiescent native profile backup is at
`~/.local/state/abdm-backups/native3-profile-20261016-001153/app-data`; do NOT restore it over
later user changes. To reach the earlier native-desktop/XWayland-integration state, first
undo this upgrade, then use the older integration rollback below. Never patch an immutable
installed release. New update/restart requests still require approval.
The release passed 28 Kotlin + 41 Python/JS tests (none skipped), recorder-only external
opener checks, native/XWayland full-app regressions and fake-home native integration checks.
The 14-case window matrix was not rerun for this file-handoff-only correction. Evidence:
`build/native-wayland/native3-installed-verification.json`, `native3-install-result.json`
and `external-open-*`. After reporting success with the update, the user requested a local
implementation commit. No push was requested; the fallback branch/history remains unchanged.

On 2026-10-15 the user explicitly requested installation and replacement of the desktop
entry to test native in real life. `scripts/native-wayland/install.py --activate-desktop`
installed the verified bundle separately; it did not use the normal Plasma installer.

- **Active KDE entry:** `~/.local/share/applications/com.abdownloadmanager.desktop`, now
  named **AB Download Manager (Native Wayland)**. Same desktop ID, no duplicate menu entry.
- Native **`1.11.0+native.3`** release: `~/.local/opt/ab-download-manager-native-wayland/releases/ABDownloadManagerNativeWayland-20261016-000710`;
  its separate `current` link and `~/.local/bin/ABDownloadManagerNativeWayland` wrapper are used.
- Persistent native profile: `~/.local/share/ab-download-manager-native-wayland/profile`.
  Data/preferences are separate; nothing was imported from the normal stores.
- Native default downloads: `~/Downloads/ABDM-Native-Wayland` (user may choose other paths).
- Launched through `kstart --application com.abdownloadmanager`; PID at verification:
  **751952**, WLToolkit/JBR, DISPLAY absent, native AWT library and installed patched UI source
  verified. Left open for real use; do not run smoke actions against that profile.
- **Original native.2 full-integration rollback** (use after undoing the native.3 upgrade):
  `~/.local/state/abdm-backups/native-integration-20261015-231430-789028/restore-integrations.sh`.
  It checks for edits/active downloads, closes native gracefully and restores pre-takeover
  browser/CLI/autostart/current/desktop selection, preserving all native history/files.
  This returns to the earlier native-desktop/XWayland-integration setup. To return the
  desktop itself to XWayland too, run the older
  `~/.local/state/abdm-backups/native-desktop-20261015-215306-b3bdb7/restore-desktop.sh`
  **after** full integration rollback, not on its own.
- **Preserved XWayland fallback:** `1.11.0+plasma.2-20261015-022633` under
  `~/.local/opt/ab-download-manager-plasma/releases/`, with its original `current`, regular
  normal user data. PID **619126** was gracefully stopped only after checking there were
  no active transfers (16 completed items); prior native PID **685378** also exited cleanly.
  The regular `ABDownloadManager`, `ABDownloadManagerCli` and `ABDownloadManagerNativeMessagingHost`
  wrappers now select native; their former contents are backed up. Recheck PIDs.
- **Browser integration now belongs to native:** seven Firefox/Chromium-family host manifests
  point to `ABDownloadManagerNativeWaylandHost`; stdio is framed protocol-only. The actual
  native GUI owns HTTP port **15151** and a separate ephemeral IPC port. The dedicated native
  CLI cold-starts the native main, never the old wrapper. Tray registration was checked via
  its actual process-owned StatusNotifier item. Native autostart uses `--background`.
- Tray, autostart, keep-awake and user-selected power actions are available again. Upstream
  **self-update remains disabled**, because its downloads can replace this runtime/renderer.
- Existing native preferences/history stayed intact except the four explicitly enabled
  API/port/tray/autostart preferences. No old database was imported; the 20 snapshotted
  old download-list/KWin files remained byte-identical.
- KWin companion and system package are unchanged; preserve them.

The earlier no-install/no-integration restrictions were superseded by the user's explicit
native desktop **and full-integration** requests. Do not migrate old stores, change fallback
binaries or the KWin companion, or interrupt the real native session without approval.
Controlled tests must use fresh build-local profiles/fake integration homes; an activated
real profile refuses internal smoke actions. Do not start the fallback concurrently without
review: its old boot can overwrite the now-native browser manifests or take port 15151.

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

The retained harness preserves the original scratch controls and adds the milestone-1
experimental artifact/cases. Neither is a root-project dependency or normal launch hook.
The separate milestone-2 builder explicitly selects the reviewed artifact only in its own
bundle. Upstream source licensing is preserved; original packaged jars are never rewritten.

Run from the real repository root after reading that README:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 scripts/wayland-probe/compile.py --overlay
PYTHONDONTWRITEBYTECODE=1 python3 scripts/wayland-probe/compile.py --native-ui
PYTHONDONTWRITEBYTECODE=1 python3 scripts/wayland-probe/compile.py
PYTHONDONTWRITEBYTECODE=1 python3 scripts/wayland-probe/run.py
```

The current matrix contains three expected native failures (including the new opt-in-off
control), eleven successes. The runner exits nonzero on unexpected outcomes OR missing
toolkit/artifact/native-protocol evidence. Inspect per-case logs and expectations, not just
the overall exit. Do not package the unsupported overlay as a hidden fix. After changing
production Kotlin, rebuild the distributable without installing before compiling probes;
otherwise packaged libraries may be stale. Exact commands are in the probe README.

## 5. Suggested implementation sequence

Milestone 1 completed initial routing, automatic native-scale bypass and the reviewable
opt-in rendering artifact. Milestone 2 audited full-app boundaries and built/tested a
self-contained image using the **complete pinned runtime**, not a mixed-vendor jlink image.
Fullscreen, real gestures/input, complete layer/focus/scaling and production-integration
acceptance remain open. Read `NATIVE-WAYLAND.md` before repeating the original plan below;
items describing absent routing/runtime packaging/full-app checks are historical.

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

Current milestone: **twenty-eight Kotlin tests + forty-one Python/JavaScript tests**, none
skipped, including the original seven Kotlin/six Python fallback regressions. The Kotlin ordering
test models lifecycle order headlessly; it is not full GUI coverage.

```sh
cd /home/alasady/Code/ab-download-manager-plasma
JAVA_HOME=/usr/lib/jvm/java-25-openjdk ./gradlew \
  -PlocalAppVersion=1.11.0+plasma.2 :desktop:shared:test :desktop:app-utils:test
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s scripts/tests -p 'test_*.py' -v
bash -n scripts/install-plasma-local.sh
git diff --check
```

Node is needed for JavaScript behavior coverage; do not call a skipped case a pass.
For full-app artifact creation/testing without installation:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 scripts/native-wayland/build.py
PYTHONDONTWRITEBYTECODE=1 python3 scripts/native-wayland/smoke.py
# Native, fresh isolated profile; no normal launcher/installer:
build/native-wayland/latest/bin/ABDownloadManagerNativeWayland
# Profile-scoped quit (also accepts --profile/--backend):
build/native-wayland/latest/bin/ABDownloadManagerNativeWayland --quit
```

`build/native-wayland/` contains the versioned bundle, build-local latest link, tar.gz,
SHA-256 sidecar and full-app/relocated-archive evidence. The renderer remains unsupported
upstream. Default disposable profiles retain integration guards; explicit desktop activation
opts into full features while self-updating stays disabled. The full-integration supervisor
`python3 scripts/native-wayland/integration_smoke.py` uses a fake home, actual browser framing,
cold native GUI start, HTTP/native-host engine downloads, real tray registration, background
reopen and a full rollback, without targeting real stores. See the native README. The
standard installer is for the approved fallback, not experimental native packaging.

Before concluding native implementation work, report what is truly native, which runtime
and renderer were used, how app state/integration were isolated, what is actually tested,
remaining unsupported features, and whether fallback regressions were checked. Don't claim
readiness or replace the approved installation based only on the smoke test.

Bootstrap publication validation (before milestone 1): seven Kotlin and six Python/JavaScript tests passed (none
skipped). The retained probe compiler and runner were exercised again: four successful
cases and the two documented DrawingSurface failures matched expectations. The native
branch bootstrap contains only docs and probe sources; runtime downloads, binaries, logs,
and test data remain ignored. The installed app and owned KWin companion were not changed.

## Suggested next-session request

> Work only in ~/Code/ab-download-manager-plasma on branch native-wayland. Read
> HANDOFF-native-wayland.md first, then WAYLAND-FEASIBILITY.md and the probe README.
> Then read NATIVE-WAYLAND.md and scripts/native-wayland/README.md for the full experimental
> app bundle. Keep the known-good XWayland fallback and intentional KWin companion. The
> full app, engine, isolated stores/IPC and complete runtime are built; native/XWayland
> download/persistence/second-launch/quit and archive-relocation tests pass. Next address
> real interactive input/gestures, native fullscreen/placement, layer/focus/scaling and
> performance before production use. The user explicitly approved native desktop activation;
> it is now installed and open with a fresh persistent profile. Preserve that real-use session,
> its downloads, the original XWayland fallback/data/integration and the KWin companion.
> Full native integration was subsequently approved and activated: browser/CLI/HTTP/tray/
> autostart now use native; the idle old app was stopped, not removed. Do not use the stale
> checkout or normal installer, migrate old data or start the fallback concurrently. Use
> fresh fake integration homes for controlled tests; review current Git status, commits and evidence.
