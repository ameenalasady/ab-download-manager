# Native Wayland implementation — full-app experimental build

Branch: `native-wayland`, worktree `~/Code/ab-download-manager-plasma`.
Read `HANDOFF-native-wayland.md` for preservation requirements and the stable fallback.
This document describes implementation after the bootstrap handoff. Native is now installed
for explicitly approved real-life testing; the original XWayland installation is preserved.

## Add Download form cohesion follow-up (installed)

The approved candidate **`ABDownloadManagerNativeWayland-20261017-180057`** is now the
user-local native release and was verified running on actual WLToolkit/Wayland from the
user desktop entry. The existing native profile and two completed downloads were retained;
no active transfers were present during activation. The separate pacman/AUR XWayland
installation and its data/integration were not changed. Desktop-only rollback:
`~/.local/state/abdm-backups/native-desktop-20261017-180532-b52575/restore-desktop.sh`.

See the newest `UI-PASS.md` section for the 600×360 form, reserved validation/status lane,
compact solutions action, explicit Size/resume presentation and isolated screenshot
reviews. 43 Kotlin tests, 41 Python tests, bundle build, both-backend full-app smoke and
real-app review passed. The essential viewport synchronization patch is unchanged in the
installed artifact (SHA-256 `24773ad1996ff30af8789631e245ebc51f7e6cc8ad0f9f0df4a50b0dccfb422f`);
no popup offsets were added. The preceding 600×340 popup refinement was installed as
174708 and is superseded by this release.

## Popup placement follow-up (historical source/build-stage notes)

See `UI-PASS.md` for the Add Download layout and picker conversion. In addition to replacing
mouse-positioned top-level picker dialogs with parent-local popups, the pinned native
ComposeWindowPanel patch now refreshes PlatformWindowContext's viewport before Swing
painting. A late Wayland graphics configuration previously updated scene density but left
LocalWindowInfo.containerSize at the old pixel size, so all popup placement/clipping used
a stale viewport. This is a rendering-boundary synchronization fix, not per-control offsets;
normal Compose/XToolkit rendering is unchanged. Window/dialog probes now require viewport/
layout equality in device pixels (verified at density 2), alongside existing protocol,
artifact, lifecycle and native-library evidence. The 14-case matrix and both-backend
full-app regression pass. Installed release, integrations and real-use data are unchanged.
Interactive mixed-monitor transitions still require acceptance.

## 2026-10-17 UI consistency pass reinstall

The user reviewed and approved the desktop UI consistency pass (see `UI-PASS.md`) and asked
to install it in place of the custom native build while leaving the pacman/AUR package
untouched. `scripts/native-wayland/install.py --activate-desktop` copied the built bundle to
`~/.local/opt/ab-download-manager-native-wayland/releases/ABDownloadManagerNativeWayland-20261017-041008`
and atomically switched the native `current` link, wrapper, icon, and user desktop entry.
This release keeps the full native integration from below (browser host manifests, CLI and
generic aliases, autostart) because they resolve through `current`; none of those files,
the separate data profile, or `/usr/bin/ABDownloadManager` were rewritten. The old idle
native.3 instance was closed gracefully first (no active transfers). Desktop-only rollback:
`~/.local/state/abdm-backups/native-desktop-20261017-042057-7ef7a3/restore-desktop.sh`
(restores the prior desktop entry only; native history/downloads are retained). Installed
and running release reports WLToolkit/Wayland, tray, HTTP `15151` and the patched UI
`37a02e916fdce9a7698776ec17d708e5052129618dcf26efde6f41ad5d165d1d`.

## Current installed: native.3 external media/file opener correction (2026-10-16)

Real-use feedback found that opening a video selected `/usr/bin/vlc`, not the user's
`~/.local/share/applications/vlc.desktop` override pointing at
`~/Code/vlc-bt-fix/install/bin/vlc`. The child inherited the experimental HOME/XDG paths,
so the same `vlc.desktop` ID resolved to the system entry and a separate VLC configuration.
This was an isolation/handoff bug, not a MIME preference or Wayland-rendering problem.

The **installed `1.11.0+native.3` build** captures the original desktop HOME/XDG/temp/
display environment before isolating ABDM, carries that allowlisted context through browser
host/CLI/detached-GUI cold starts, and restores it only for external-app helpers after
validating explicit desktop integration. Unset variables are removed, not kept as profile
paths. ABDM's own JVM/data/IPC remain isolated; internal detached launches do not use this
external-app override. Linux URL opening uses the explicit child-process path instead of
AWT/libgio when a scoped desktop environment is required. Ordinary launches and disposable
nonactivated profiles keep their previous behavior. No VLC entry or MIME default is edited.

`external_open_smoke.py` invokes the real bootstrap and public file/folder/URL openers in a
non-window JVM against a fake desktop, with a same-ID system/custom application pair and
recorder-only handlers. It checks custom override selection, spaces/Unicode in paths,
original HOME/custom XDG values, removal of unset variables/internal context, and retained
parent isolation. It does not launch real VLC or exercise the user's downloaded video.
Initial fixture runs exposed test-only desktop-detection/Exec-quoting issues; the final
fixture explicitly uses XDG's generic backend and validates its MIME defaults before opening.
The native GUI integration supervisor also supplies its fake desktop environment to manifest
installation and verifies the original context survives a real native-host cold start.

Validation passed on candidate `ABDownloadManagerNativeWayland-20261016-000710`: **28 Kotlin
and 41 Python/JavaScript tests**, none skipped, the recorder-only external opener check,
both-backend full-app download/reopen/IPC/quit regression, and fake-home native-host/HTTP/
tray/autostart/cold-start/rollback integration suite. The existing native PID **730009**,
installed current link, VLC desktop entry, MIME settings and integration policy stayed
unchanged. Final evidence: `build/native-wayland/external-open-final-verification.json`.
The earlier 14-case window matrix was not rerun for this file-handoff-only correction.

The user then **explicitly approved installing the fix and a graceful idle-only restart**.
Both persisted status and read-only live RPC confirmed nine completed items, no active
transfers. Native.2 PID 730009 exited gracefully. The tested immutable release was copied to
`~/.local/opt/ab-download-manager-native-wayland/releases/ABDownloadManagerNativeWayland-20261016-000710`
and selected by the native `current` link. Launched through the normal desktop ID, native.3
PID **751952** verified actual WLToolkit/JBR, installed patched UI, DISPLAY absent, separate
stores, HTTP **15151**, IPC **34321**, tray and framed host/HTTP ping. The original desktop
context correctly records `/home/alasady`, unset default XDG paths and `DISPLAY=:0`, while
ABDM itself retains the private HOME. All seven browser manifests are logically unchanged.

Settings/history were identical immediately after installation; the GUI loaded all nine
items. Subsequent live-session changes (trackDeletedFilesOnDisk enabled and history cleared)
were observed during verification and retained, not overwritten with old snapshots. The
3767 protected fallback/data/KWin/VLC/default-association files stayed byte-identical. No
real video playback was automated; the user should retry Open for real VLC acceptance.

**Upgrade rollback to fully integrated native.2:**
`~/.local/state/abdm-backups/native-integration-20261016-001153-bf4f31/restore-integrations.sh`.
It checks edits/active transfers and retains native history/downloads. The original full
integration rollback below applies only after undoing this upgrade. A quiescent safety copy
is at `~/.local/state/abdm-backups/native3-profile-20261016-001153/app-data`; do not restore it
over later real-use changes. Real-use native.3 is left open: no smoke actions or further
restarts without approval. Installed evidence:
`build/native-wayland/native3-installed-verification.json` and `native3-install-result.json`.
The user subsequently reported success and requested a local commit of the native work;
source/tooling/tests/docs are versioned on `native-wayland`, without publishing build artifacts.

## Earlier full native integration takeover (native.2, 2026-10-15)

The user asked why browser capture and other features still used XWayland and requested
**fully native use for thorough testing**. That superseded the initial integration-off
boundary. Native **`1.11.0+native.2`** was selected for **all ordinary entrypoints** before
the native.3 upgrade above:

- Desktop and regular `ABDownloadManager` command select the native main; regular CLI and
  native-host commands select dedicated native wrappers. The host has protocol-only stdout
  and supports the browser's real length-prefixed JSON framing without launcher banners.
- All seven Firefox/Chromium-family manifests use `ABDownloadManagerNativeWaylandHost` and
  the existing extension IDs/origins. The profile's own RPC and dedicated CLI/main handle
  browser cold starts; there is no fallback launch route through the old binary.
- At takeover, native PID **730009**, release `ABDownloadManagerNativeWayland-20261015-230905`, owns HTTP
  port **15151** plus independent IPC, native WLToolkit/library and the installed patched UI.
  Its StatusNotifier tray registration and real autostart `--background` entry were verified.
- Tray, autostart, keep-awake and user-selected power actions are enabled normally. No power
  action was invoked in tests. **Upstream self-updating remains disabled**: its packages do
  not preserve this JBR/renderer. Do not enable it as a shortcut to native updates.
- Existing native data/preferences were retained; only API/port/tray/autostart settings
  changed. Old data was not imported. Old XWayland PID **619126** and prior native PID
  **685378** exited gracefully after idle checks; the old 16 completed download records
  and KWin files (20 snapshotted files) remained byte-identical. Fallback binaries remain.

Activation: `python3 scripts/native-wayland/desktop_integration.py --activate`. It validates
an integration-capable bundle before stopping anything, refuses active transfers/unowned
native helpers, backs up entrypoints, then adds a canonical profile-local
`.abdm-desktop-integration.properties` policy. That explicitly permits real-home integration
while keeping HOME/XDG/data isolated. Dedicated native launch paths are checked, not inferred
from jpackage, DISPLAY or a Wayland handle. Updates stay blocked regardless of this policy.
Default/fresh probe profiles have no policy and retain all earlier isolation guards.
`--smoke-test` is refused on activated profiles, and disposable integration tests carry a
**DISPOSABLE INTEGRATION TEST** title suffix.

Full rollback:
`~/.local/state/abdm-backups/native-integration-20261015-231430-789028/restore-integrations.sh`.
It refuses edits/active transfers, gracefully exits native and restores the prior
native-desktop/XWayland-integration selection without deleting native history/files. To
return the desktop to XWayland too, run the earlier desktop-only restore **after** that
full rollback; the desktop-only script alone no longer reverses the wrapper/manifest switch.

`integration_smoke.py` passed against a fake integration home: real framed ping and add,
cold-started native dialog, actual PID-owned tray, HTTP and native-host 1 MiB engine downloads
with checksum verification (including category subfolders), background reopen/persistence,
second launch, graceful quit and complete rollback. Real user files were not targets.
Installed-profile checks then verified native port ownership, CLI version, all manifests,
tray/autostart and no old GUI process. Browser UI clicks/reconnection are still real-user
acceptance, not claimed by this protocol-level test. **23 Kotlin and 38 Python/JavaScript
tests**, the 14-case window matrix and basic full-app regression suite pass, none skipped.
Evidence: ignored `build/native-wayland/desktop-integration-results.json`,
`full-integration-installed-verification.json`, and integration build/test/matrix logs.

## Earlier desktop-only activation (2026-10-15, historical)

After reviewing the full bundle, the user explicitly requested installation and replacement
of the desktop app. `scripts/native-wayland/install.py --activate-desktop` now performs that
separate, reversible activation. It verifies/copies the immutable bundle, creates a durable
profile, installs only a new native wrapper/icon and replaces the existing **same-ID**
`com.abdownloadmanager.desktop` override, validating it and refreshing desktop/KDE caches.
It never invokes the normal installer or stops a running app. Eight disposable installer/
rollback tests passed; the Python/JavaScript suite now contains **31 tests**, none skipped.

- Native image: `~/.local/opt/ab-download-manager-native-wayland/current` ->
  `releases/ABDownloadManagerNativeWayland-20261015-035751`.
- Desktop name: **AB Download Manager (Native Wayland)**; wrapper:
  `~/.local/bin/ABDownloadManagerNativeWayland`.
- Persistent profile: `~/.local/share/ab-download-manager-native-wayland/profile`;
  initial download destination: `~/Downloads/ABDM-Native-Wayland`.
- Launched via the normal desktop ID; verified actual WLToolkit, JBR, native AWT library,
  DISPLAY unset, installed UI code source, separate data and only independent IPC listener.
  Native PID **685378** was left open for user testing.
- Desktop rollback:
  `~/.local/state/abdm-backups/native-desktop-20261015-215306-b3bdb7/restore-desktop.sh`.
  It refuses later edits rather than overwriting them, restores the prior desktop entry
  and refreshes caches, retaining all native downloads/profile/binaries.

The original XWayland release/current link, regular wrappers, normal data, autostart,
browser manifests and intentional KWin companion were untouched (45 protected file hashes
match). Old PID **619126** was not stopped, avoiding disruption to existing downloads.
Browser-extension traffic still belongs to that old app. Native integration/tray/autostart/
updates/keep-awake/power remain disabled; the desktop switch does not remove those safety
boundaries. Do not run internal smoke actions on this now real-use native profile.

## Milestone 2: complete, isolated application bundle

The user requested the **full native app build** after the window experiments. There is
now a self-contained **Linux x64 `1.11.0+native.1`** bundle with the real application
entrypoint, UI, DI, download engine, persistence, native libraries/resources and complete
checksum-verified **JBR `25.0.4.1+1-b635.72`** runtime. It is not another standalone probe.
The pinned patched Compose 1.12.1 + SOFTWARE/SwingGraphics renderer is still experimental.
The normal Gradle dependency/distributable retains the unmodified upstream UI jar.

Build/launch details: [`scripts/native-wayland/README.md`](scripts/native-wayland/README.md).
From this checkout:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 scripts/native-wayland/build.py
build/native-wayland/latest/bin/ABDownloadManagerNativeWayland
# Quit only that default experimental profile, never the installed application:
build/native-wayland/latest/bin/ABDownloadManagerNativeWayland --quit
```

Build output is ignored under `build/native-wayland/`: versioned bundles, a build-local
`latest` link, and `ABDownloadManager-native-wayland-experimental-linux-x64.tar.gz` with a
SHA-256 sidecar. The launcher verifies the whole bundle, selects actual WLToolkit with
DISPLAY unset and explicit native rendering opt-in, and refuses silent backend fallback.
A diagnostic `--backend x11` control retains the original rendering selection. No normal
installer, launcher replacement, desktop registration or installed-process restart occurs.
The archive also launches after relocation; it does not require this checkout or a system
JVM. Python 3.11+ and the usual Linux shared libraries are still required.

### Full-app isolation audit and boundaries

Startup, single-instance negotiation, preferences and integration actions were reviewed
before booting AppKt. `ExperimentalDesktopProfile` validates a marked, user-owned launcher
profile's canonical paths, HOME/user.home, XDG directories and Java/native temp paths
**before AppInfo, stores or IPC discovery**. Both launcher and Kotlin reject symlinked
application data. AppInfo explicitly selects that profile's `app-data`; no normal database
or preferences are imported. The native entrypoint/app identity and experimental title
suffix distinguish it from the installed instance. Non-window CLI/native-host entrypoints
do not initialize AWT just to validate a profile or inspect IPC.

Each profile has an independent OS-assigned loopback single-instance port and config/lock
files. In default nonactivated profiles, the browser-integration server is not started, even
if its preference is changed.
Browser manifest install/removal and desktop-entry actions return without side effects.
Autostart uses a no-op implementation; tray, update checks/download/apply/cleanup,
keep-awake and power actions are disabled at application boundaries. Settings toggles do
not re-enable them by themselves. The later explicit integration policy described above
permits desktop features, but not self-updating. Normal nonexperimental launches retain their behavior.

This is **application-level isolation, not an OS/security sandbox**: intentional downloads,
file dialogs and external-app commands retain the user's ordinary permissions. Do not
import old production data blindly or use this for important downloads without considering
the experimental renderer. Browser/tray/autostart require the explicit integration policy;
updating remains unavailable.

### Full-app results and a discovered shutdown bug

The native and XWayland full-app controls both pass the following bounded tests, in the
original bundle **and again from a checksum-checked archive extracted at a new path**:

- Real home, settings and URL-entry windows draw; maximize/restore and extra-window disposal
  complete. Runtime, actual toolkit, patched UI code source, draw callbacks and Wayland
  native library/buffer attach/commit evidence are required, not just exit 0.
- The **real download engine** downloads a disposable 1 MiB payload from a supervisor-owned
  127.0.0.1 HTTP fixture into the profile's Downloads folder. The app and supervisor both
  verify SHA-256 `fbbab289f7f94b25736c58be46a994c441fd02552cc6022352e3d86d2fab7c83`.
- Integration/autostart/tray preferences are toggled and reset; direct browser-manifest and
  desktop-entry actions remain no-ops. Only the profile's independent IPC listener exists,
  not the usual browser-integration port. Preferences flush on graceful exit.
- Reopening the same profile loads its persisted completed download. A second launcher
  reaches **that** running instance and exits without starting another GUI; `--quit` uses
  its own profile-scoped IPC. Native and XWayland sessions dispose cleanly and remove their
  IPC port files. A non-window CLI check also succeeds with deliberately unavailable displays.
- Protected production stores/integration/desktop/autostart/KWin file hashes, installed
  release link and installed PID remain unchanged. No global input injection is used.

The initial full-app run reached smoke completion but exposed a real JBR shutdown error:
direct `exitProcess(0)` left Compose windows for WLToolkit's off-EDT shutdown hook, causing
`Swing hierarchy should be accessed only from AWT Event Dispatch Thread`. Experimental
exit now stops the engine and flushes settings, then asks Compose's application scope to
exit on `Dispatchers.Main`, disposing windows before JVM shutdown. Normal app exit remains
unchanged. The supervisor rejects uncaught GUI/shutdown failures even when exit is 0.

Current validation includes **23 Kotlin tests**, **38 Python/JavaScript tests**, none skipped,
and the **14-case window matrix** below (11 successes, 3 deliberate DrawingSurface
failures). Full-app/relocated-archive evidence and protected-file snapshots remain ignored
under `build/native-wayland/`; window evidence stays under `build/wayland-probe/`.
Build-only creation and fallback regressions pass; the stable branch/original installed app
and intentional KWin companion have not been changed. The later user-approved desktop
activation is described above. Implementation, tooling, tests and documentation are versioned
on `native-wayland`; generated runtimes, bundles, profiles and evidence remain ignored.

## Milestone 1: window/backend foundation

### Backend-safe window routing

`desktop/shared/.../window/AwtWindowBackend.kt` classifies the **initialized AWT toolkit**,
not DISPLAY, WAYLAND_DISPLAY, XDG_SESSION_TYPE, or the requested toolkit property:

- `sun.awt.X11.XToolkit`: existing X11/EWMH behavior, including under XWayland;
- `sun.awt.wl.WLToolkit`: native Wayland;
- anything else, including headless/unknown: not authorized to use X11 window helpers.

`LinuxWindowState.kt` checks this before reading Compose's window handle or issuing a
fullscreen/move/disposal request. `X11Ewmh` independently gates its display connection,
including calls made with **nonzero** handles. The XToolkit native-unmap/XSync disposal
fix and all CustomWindow lifecycle/drag code are retained. The outer disposal effect was
not moved or replaced. Native Wayland never withdraws its window through Xlib.

Automatic `SystemUiScale` correction now skips native Wayland before reading any global
XWayland desktop-scale hints. JBR's per-monitor transform remains authoritative. Explicit
`abdm.systemUiScale` overrides retain their original meaning; the app's own UI scale and
separate font scale are not changed. This prevents double compensation, but is **not**
proof of correct fractional/mixed-monitor scaling.

### Pinned, explicit experimental rendering artifact

The stock ComposeWindow failure remains reproducible. There is no supported window-level
Swing rendering switch in the pinned Compose 1.12.1 source; the current upstream
[ComposeWindowPanel source](https://github.com/JetBrains/compose-multiplatform-core/blob/jb-main/compose/ui/ui/src/desktopMain/kotlin/androidx/compose/ui/awt/ComposeWindowPanel.desktop.kt)
also hardcodes SkiaSurface and warns against standalone Swing rendering (reviewed during
this milestone; that moving branch is not a build input).

`scripts/wayland-probe/compose-1.12.1-native-wayland.patch` is a small, reviewable local
Compose change selecting SwingGraphics **only** with explicit
`abdm.experimental.nativeWayland=true` AND an actual WLToolkit window. It covers the
shared ComposeWindowPanel used by both ComposeWindow and ComposeDialog. XToolkit retains
SkiaSurface even when the experimental property is true.

`compile.py --native-ui` verifies source and binary hashes, preserves upstream licensing
and Kotlin module metadata, replaces only that class family in a **new** jar under ignored
`build/wayland-probe/`, and records provenance hashes. The runner substitutes that jar for
the original dependency only in named experimental cases. It does not shadow UI classes
on top of the original jar. The historical unconditional class overlay remains only as a
named comparison control. Neither mechanism is wired into Gradle app dependencies or the
normal launcher/distributable.

An important harness correction: `pathing-classpath.jar` is a launcher-only manifest
pointing back at all original dependencies. Including it in the explicit test classpath
can resolve the stock UI before the replacement. It is now excluded, and the runner
requires the expected UI code source. An initial experimental run caught this provenance
failure; it was corrected before the successful matrix reported below.

This is still an **unsupported local Compose experiment**, not a supported upstream
renderer. Milestone 1 did not produce a native app package; milestone 2 above explicitly
packages the reviewed artifact rather than changing the normal distribution. The explicit flag alone does nothing in an unpatched
production distribution. See the [probe README](scripts/wayland-probe/README.md) for exact
checksums, preparation and commands.

## Milestone-1 isolation and measured results

Runtime/renderer used:

- Official, checksum-verified JBR **25.0.4.1+1-b635.72**, native `sun.awt.wl.WLToolkit`.
- Compose **1.12.1**, Skiko **0.150.1**, Kotlin/Compose compiler **2.4.20**.
- `SKIKO_RENDER_API=SOFTWARE`, with the experimental SwingGraphics path for standalone
  native ComposeWindow/ComposeDialog. No GPU acceleration or performance claim.
- XWayland controls use both JBR's XToolkit and system OpenJDK25 **25.0.4.1** with stock
  Compose. The patched UI's flagged XToolkit control also uses the original SkiaSurface.

During milestone 1, only `WaylandSmokeKt` was started. ABDM's AppKt, engine, settings/data stores, integration
server, single-instance negotiation, tray, autostart, browser-manifest code and broadcasts
were never started. Every case has independent HOME/user.home/XDG directories under
`build/wayland-probe/cases/<case>/`. Native titles and `WaylandSmokeKt` app identity are
unmistakably probes. No global mouse/keyboard injection was performed. The installed app
was not restarted, updated, replaced, or used as a test target.

The retained matrix has **14 expected outcomes: 11 successes and 3 intentional
DrawingSurface failures**, all matching expectations:

- Original native ComposePanel succeeds; stock native ComposeWindow fails, including with
  the panel-only Swing property. Historical overlay window/wrapper controls still work.
- Explicit experimental native ComposeWindow and actual ABDM CustomWindow succeed with
  DISPLAY unset, native AWT library loaded, and real Wayland buffer attach/commit traffic.
- A mixed-environment CustomWindow run retains `DISPLAY=:0` and WAYLAND_DISPLAY. It still
  uses WLToolkit and proves every low-level X11 entry is a no-op with nonzero fake handles.
  White-box checking confirms **no XOpenDisplay attempt**, not just a false query result.
- CustomWindow exercises resize, the first vetoed AWT WM-close, maximize/restore, second
  accepted close **from maximized**, composition disposal and AWT disposal. The native
  public X11 helpers are also tested as no-ops with a realized, maximized window.
- A modeless ComposeDialog renders, resizes after a vetoed close, accepts the second close,
  disposes, and leaves its owner alive until explicitly disposed by the probe.
- Patched native UI with opt-in disabled reproduces the DrawingSurface failure. The same
  patched jar with opt-in enabled under XToolkit succeeds with a **nonzero native handle**,
  confirming original rendering is retained there.
- JBR and ordinary OpenJDK25 XWayland CustomWindow controls pass. On ordinary OpenJDK25,
  JBR move is unavailable while EWMH move remains available on KWin, covering the fallback
  routing used by the approved installation.

AWT close events and placement changes are **programmatic**, not compositor-originated
input. Popups are opened by timed Compose state changes. Drawing counts are callbacks,
not FPS. The suite requires runtime/toolkit, artifact-source, library/protocol and lifecycle
markers in addition to exit codes; it does not treat an unrelated crash as an expected
DrawingSurface failure.

Regression/build validation:

```sh
JAVA_HOME=/usr/lib/jvm/java-25-openjdk ./gradlew \
  -PlocalAppVersion=1.11.0+plasma.2 \
  :desktop:shared:test :desktop:app-utils:test :desktop:app:createReleaseDistributable
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s scripts/tests -p 'test_*.py' -v
PYTHONDONTWRITEBYTECODE=1 python3 scripts/wayland-probe/compile.py --overlay
PYTHONDONTWRITEBYTECODE=1 python3 scripts/wayland-probe/compile.py --native-ui
PYTHONDONTWRITEBYTECODE=1 python3 scripts/wayland-probe/compile.py
PYTHONDONTWRITEBYTECODE=1 python3 scripts/wayland-probe/run.py
bash -n scripts/install-plasma-local.sh
git diff --check
```

Thirteen Kotlin tests (the original seven plus five backend/scaling tests and a headless
nonzero-handle guard test) and fifteen
Python/JavaScript tests (the original six plus nine artifact/evidence tests) passed, none
skipped. Build-only release creation succeeded; no installation was performed. GUI smoke
results and protocol/library evidence are in ignored `build/wayland-probe/`. The installed
release/PID and the three owned KWin companion file hashes still match the handoff snapshot.
Existing fallback animation evidence remains historical; this milestone did not visually
re-instrument or claim new animation acceptance.

## Still unsupported or unvalidated

- **Production readiness/integration:** the full app now runs in a separately audited
  profile, not against the old production database. Full browser/CLI/tray/autostart/keep-awake/
  power support is explicitly activated for real-life testing. Self-updating stays disabled;
  protocol/registration tests are not complete browser-UI or general production acceptance.
- **Runtime packaging:** a complete pinned runtime is bundled, not a minimized jlink image
  or a signed production installer. OS/distro portability beyond this host remains unproven.
- **Real gestures/input:** title-content dragging, restore animation, titlebar buttons and
  double-click, WM close/fullscreen, keyboard/text input, IME, clipboard, external DnD and
  native file dialogs have not been accepted. API availability is not a gesture test.
- **Fullscreen:** SwingSkiaLayerComponent still returns false and ignores fullscreen sets;
  native handle is 0. EWMH fullscreen queries intentionally return false on WLToolkit.
  Native compositor fullscreen detection/exit, geometry persistence and drag behavior need
  a real native solution, not X11 calls or reuse of the zero handle as a safety guard.
- **Layers/focus/transparency:** only the existing timed popup and modeless top-level dialog
  lifecycle were exercised. Modal dialogs, separate-window layer settings, popup focus,
  transparency, accessibility, activation, notifications and actual tray-menu interaction
  still need broader coverage; PID-owned tray registration and browser URL dialogs now pass.
- **Scaling/positioning:** global automatic correction is disabled, but fractional/mixed-DPI
  transitions, compositor-controlled absolute placement, ScreenGeometry, stored bounds and
  font behavior have not been accepted. Do not restore X11 placement assumptions blindly.
- **Performance:** the Swing path copies each frame. A realistic large-window CPU/memory
  baseline is still required; successful drawing does not establish usable frame rate.

Next work should add explicit interactive acceptance of the isolated full application
(without global input affecting the installed app), native fullscreen/placement design,
renderer/layer coverage and a CPU/memory baseline before proposing production use. Keep changes opt-in and reviewable. Do not merge this
experiment into the stable fallback, install/promote it, remove the intentional KWin
companion, or change the installed desktop entry/data without user approval.
