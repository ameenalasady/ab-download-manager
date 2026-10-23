# HANDOFF: ABDM Plasma fullscreen/maximized window behavior

**Next task:** read `HANDOFF-cleanup-audit.md` for the user's requested redundancy/dead-code
review. Window behavior is working and installed; this file is the investigation history.

Date: 2026-10-15 ~01:00 (user's TZ). Author of this doc: previous agent (Muse Spark).
Status update (2026-10-15 ~01:40): **both fixes accepted by user; final build installed
as the normal KDE desktop launcher. No currently reported open bug.** Read §0c first,
then §0b for the intentional KWin companion. Earlier policies/releases are historical.

## 0c. Final desktop installation (2026-10-15 ~01:40)

- User approved the drag result ("amazing workkkk") and asked to replace the desktop
  launcher with this build. Ran the standard installer with explicit JDK 25 and
  `ABDM_LOCAL_VERSION=1.11.0+plasma.2` successfully, including companion refresh.
- CURRENT release: `1.11.0+plasma.2-20261015-013921`; PID `606244` (no --background;
  started using the actual KDE launcher). Backup:
  `~/.local/state/abdm-backups/install-20261015-013918/`.
- User-local `~/.local/share/applications/com.abdownloadmanager.desktop` overrides the
  identical system desktop ID. Exec points to `~/.local/bin/ABDownloadManager` ->
  `current`. No duplicate desktop ID, package edits, or root install needed.
- `kstart --application com.abdownloadmanager` launched the NEW release (verified
  `/proc/606244/exe`). CLI version correct, main window correct WM_CLASS, autostart,
  native messaging and KDE application cache refreshed.
- Intentional ABDM-only maximize companion remains installed and loaded; scale is stock.
  `pacman -Qkk ab-download-manager-bin`: 412 files, 0 altered. appSettings unchanged
  from backup. Desktop validation and git whitespace check pass. No commits/staging.
- Worktree remains the real `ab-download-manager-plasma` branch, with all fixes unstaged
  above the user's staged plasma.1 work. See PLASMA-LOCAL.md for rebuild/undo instructions.

## 0b. Restore-on-drag animation: fractional-size equality bug (2026-10-15 ~01:38)

- User confirmed the corrected close animation: "omg this is perfect". Do not regress it.
  New nitpick: dragging maximized titlebar restored instantly rather than animating.
- Real Wayland input IS available: `ydotoold` PID 1354, socket
  `/run/user/1000/.ydotool_socket`; use `YDOTOOL_SOCKET` explicitly. Relative mouse moves
  work; absolute movement clamps at origin here. Acceleration changes distances: verify
  actual position using `kdotool getmouselocation` before clicking. xdotool is still dead.
- KWin's stock maximize effect starts the restore, then cancels 25 ms later because
  a move changes reported logical width from `1120.8000000000002` to `1120.8`.
  The strict equality checks in `onWindowFrameGeometryChanged` misclassify roundoff as
  a resize. Native EWMH move handoff is correct; don't delay/fake the app's restore.
- An ABDM-only 1e-6 logical-pixel tolerance in the two size guards fixes it: the same
  real drag completes the full animation (~276 ms), with no CANCEL-GEOMETRY.
  Other apps keep exact stock comparisons; real resizes still cancel normally.
- Installed no-logging user-local companion effect at
  `~/.local/share/kwin-wayland/effects/maximize/`, with ownership marker
  `.abdm-maximize-workaround.json` recording stock/generated hashes. THIS OVERRIDE IS
  NOW INTENTIONAL. Do not delete it as if it were a temporary logging shadow.
  `scale` remains stock. App binary remains `...-012340`, PID `591630`.
- Reproducible helper in repo: `python3 scripts/plasma-maximize-workaround.py install`.
  Undo: `python3 scripts/plasma-maximize-workaround.py remove` (reloads stock immediately).
  It refuses unrelated/edited overrides and unknown upstream layouts. Re-run after KWin
  upgrades to refresh from stock; remove if upstream changes need review or fix the bug.
  The regular app installer refreshes ONLY an already-owned opted-in companion.
- Six new Python/JavaScript tests pass, including app isolation, real resize vs numerical
  noise, ownership and upgrade-layout guards. Existing seven Kotlin tests still pass.
  No new Kotlin changes for this nitpick. No commits or index modifications.
- Evidence: `/tmp/probe/title-drag-20261015-013038/` (cancel 25 ms) and
  `...-013214/` (full 276 ms). `/tmp/probe/trace-title-drag.py` drives real pointer input
  and logs all maximize geometry/move events. `ABDM_FUZZY_MAXIMIZE=1 ABDM_SKIP_KONSOLE=1`
  enables the experimental patch. It refuses an existing override; preserve or remove
  the owned companion explicitly before using. The attempted Konsole comparison was
  inconclusive (probe didn't actually maximize); ABDM's cancellation trace is decisive.
- Close reverified WITH the companion: normal scale/fade from maximized geometry, no
  restore signal, ~222 ms. `/tmp/probe/animated-close-20261015-013509/`.
  `/tmp/probe/trace-animated-close-preserve.py` (`ABDM_PID=<pid>`) temporarily instruments
  and then restores the owned companion rather than deleting it. Earlier trace drivers
  deliberately refuse an existing override; don't bypass that guard casually.
- Next step: user visual test of titlebar drag from maximized with the companion loaded.
  Keep close behavior unchanged. No need to rebuild/reinstall the binary for this fix.

## 0a. Final behavior correction: animate, do NOT suppress (2026-10-15 ~01:26)

- User tested `...-011542`: floating closes animated normally, maximized closes simply
  disappeared. Clarified that this is NOT wanted: normal close animation must play from
  maximized geometry, without an initial restore. Do not equate instant disappearance
  with a successful fix. The native-unmap ordering fix is right; skip-close is not.
- Removed ALL `_KDE_NET_WM_SKIP_CLOSE_ANIMATION` writes/helpers, including the quit
  workaround. `prepareForWindowDisposal` now only unmaps + XSync for enlarged windows.
  Kept outer effect ordering and close gesture guard. Float/normal maximize/drag paths
  remain untouched. No KWin animation policy override is needed.
- Latest installed release: `1.11.0+plasma.2-20261015-012340`; running PID `591630`.
  Backup: `~/.local/state/abdm-backups/install-20261015-012337/`. Built/tested/installed
  with JDK 25 using the standard installer. All seven tests pass.
- Temporary logging-only shadows of BOTH `maximize` and `scale` proved:
  - Maximized X-close/WM-close: no ABOUT/CHANGED restore signals during close.
  - Scale CLOSE-ANIMATED sees unchanged maximized geometry, skipClose=false, and
    real scale/opacity animation IDs. Deleted window retained for ~220 ms.
  - Fullscreen close, floating close, and maximized process quit also animate normally
    from unchanged geometry. No intermediate unmaximize/unfullscreen is requested.
- Evidence directories `/tmp/probe/animated-close-20261015-012435/` (max X, 223 ms),
  `012449` (WM-close, 221 ms), `012453` (fullscreen, 214 ms), `012459` (floating,
  222 ms), `012505` (quit, 226 ms). Driver `/tmp/probe/trace-animated-close.py`:
  `ABDM_PID=<pid>`, optional
  `ABDM_TEST_MODE=maximized-wmclose|fullscreen-click|floating-click|quit`.
  It asserts no close-time restore, unchanged effect geometry, and a real close
  animation. Requires both stock effects initially loaded and no existing shadows.
- Both temporary shadows removed; stock maximize and scale loaded. No kwinrc edits,
  no commits or index modifications. appSettings.json byte-identical to latest backup.
- Dashboard reopened maximized for user retest (`0x01000007` when last checked).
  Next step: get human confirmation that maximized X-close now plays the proper close
  effect rather than restore-then-close OR instant disappearance.

**§0 below documents root-cause discovery but its skip-animation policy, PID, release,
helper descriptions, and pending test instructions are superseded by §0a.**

## 0. Decisive follow-up: AWT restores before unmapping (2026-10-15 ~01:18)

- User watched the two-pass test on the guarded `...-004612` build:
  A (normal effects) still visibly restored then disappeared;
  B (ONLY `maximize` unloaded) disappeared with no animation. Effect restored afterward.
  This disproves the close-guard-as-fix theory and isolates the maximize effect.
- A temporary logging-only shadow under
  `~/.local/share/kwin-wayland/effects/maximize/` caught the exact close sequence:
  `ABOUT(false,false)` -> floating `GEOMETRY` -> `CHANGED(false,false)` -> `CLOSED`
  in about 1 ms, all with `skipsCloseAnimation=true`. Then the maximize effect's
  size/translation/crossfade animations held the deleted window for ~250 ms.
  Trace: `/tmp/probe/maximize-trace-20261015-011225/`.
- **Cause proven in OpenJDK 25 bytecode:** `sun.awt.X11.XFramePeer.setVisible(false)`
  explicitly calls `XWM.setExtendedState(state & ~6)` BEFORE delegating to hide/unmap.
  `ComposeWindow.dispose()` -> `JFrame.dispose()` -> AWT hide triggers this. AWT
  dispose is NOT just unmap+destroy. `/tmp/probe/XFramePeer.javap` lines 889–921.
- Thus historical F1/F5 and the inference in O5 below were too strong: 30 ms geometry
  polling aliased a ~1 ms pre-close restore, while KWin retained and animated its pixels.
  KWin's release/destroy bookkeeping was not the trigger; AWT's pre-unmap request was.
- Fix in the real worktree (unstaged, nothing committed):
  - `X11Ewmh.unmap(xid)` and `LinuxWindowState.prepareForWindowDisposal(window)` arm
    skip-close and withdraw maximized/tiled/fullscreen windows, then XSync before AWT
    teardown. KWin processes unmap before AWT's later restore client message.
  - An OUTER `DisposableEffect` registered AFTER `Window` in `CustomWindow` runs first
    on disposal (reverse registration order). The native window reference is published
    from content via `SideEffect`. Do not use a content-level onDispose: ComposeWindow
    disposes JFrame BEFORE its content. Do not unmap on a mere close request: it may
    be vetoed. Floating closes and normal maximize/restore/drag paths are unchanged.
  - `WindowDisposalOrderTest.kt` pins the reverse nested-effect disposal ordering in a
    headless Compose test. Seven tests now pass.
- Installed via `ABDM_LOCAL_VERSION=1.11.0+plasma.2 scripts/install-plasma-local.sh`
  with JDK 25. Current release: `1.11.0+plasma.2-20261015-011542`, PID `580065`.
  Latest install backup: `~/.local/state/abdm-backups/install-20261015-011539/`.
- Runtime verification on the installed fix, with maximize effect LOADED:
  maximized X-close (`...-011639`), WM-close (`...-011650`), fullscreen X-close
  (`...-011656`) all emit `CLOSED`/`DELETED` at original geometry, NO `ABOUT`,
  NO `CHANGED`, NO maximize-animation IDs. Floating close (`...-011702`) keeps
  skip=false and its ordinary close animation; normal restore still animates.
- Post-fix maximize-button and titlebar-double-click round trips preserve the exact
  floating rectangle `(685,345 1401x727)`; maximize button exits WM-fullscreen to the
  underlying maximized state. Dashboard left open maximized (`0x01000090`) for the
  human retest. No A/B helper is running.
- All temporary effect shadows removed; stock maximize and scale are loaded. No kwinrc
  edits. appSettings.json still has 37 keys. Only normal window-state persistence changed.
- Tools:
  - `/tmp/probe/trace-maximize-close.py`: reversible instrumentation + synthetic close;
    set `ABDM_PID=<pid>` and optionally
    `ABDM_TEST_MODE=maximized-wmclose|fullscreen-click|floating-click`.
  - `/tmp/probe/close-effect-ab.py`: human two-pass helper (still hardcodes OLD PID
    545802; update before reuse). Human A/B log: `/tmp/probe/close-effect-ab-retry.log`.
- Next step: user visual retest of maximized X-close on the current release, with all
  stock effects enabled. If confirmed, update this status. Do not disable maximize as
  a permanent workaround, and do not revisit the old bounce/scale theories.

**Sections 1–9 below are the preserved pre-discovery investigation log. Where they
conflict with §0 (notably installed PID/release, F1/F5, O5, or the remaining bug), §0
supersedes them.**

## 1. Environment (historical; read §0 first)

- Machine: Arch Linux, KDE Plasma 6 / Wayland session (`XDG_SESSION_TYPE=wayland`,
  `XDG_CURRENT_DESKTOP=KDE`, KWin 6.7.5). Two monitors (DP-3 primary 2560x1440 left,
  DP-1 2400x1350 right).
- The app is an X11 client on XWayland (no Wayland AWT exists). `DISPLAY=:0`.
- Bundled runtime is **plain Arch OpenJDK 25, NOT JBR** (verified via
  `jcmd <pid> VM.system_properties`: `java.vendor=Arch Linux`). Consequences:
  - `JBR.isWindowDecorationsSupported()` = false → windows are UNDECORATED with a custom
    Compose title bar.
  - `JBR.getWindowMove()` = null → title-bar drag uses Compose's `WindowDraggableArea`
    fallback = `StandardMoveHandler` = AWT mouse listeners + `window.setLocation()`.
    (Compose ignores `setLocation` targets on maximized/fullscreen windows, so drag was a
    silent no-op there.)
  - `SKIKO_RENDER_API=SOFTWARE` is exported by the launcher wrapper.
- **TWO TREES — WORK IN THE RIGHT ONE:**
  - `~/Code/ab-download-manager` = STALE checkout (Aug 22 + old uncommitted plasma work).
    DO NOT BUILD FROM IT. (I did once by mistake; see §8. It also has my scratch port of
    the fixes, inert, plus build outputs. Leave it alone.)
  - `~/Code/ab-download-manager-plasma` = THE REAL TREE (branch
    `local/plasma-wayland-1.11.0`, upstream v1.11.0 `5b2cffe4`, compose 1.12.1).
    All real work is here, currently **unstaged** (staged `A/M` entries = plasma.1 work).
- Install layout: `~/.local/opt/ab-download-manager-plasma/releases/<ver>-<ts>/` +
  `current` symlink. Launcher `~/.local/bin/ABDownloadManager` (follows `current`).
  **Build+install ONLY via** `cd ~/Code/ab-download-manager-plasma &&
  ABDM_LOCAL_VERSION=1.11.0+plasma.2 scripts/install-plasma-local.sh`
  (does version check, tests, graceful exit, data backup, release install, .desktop /
  autostart / native-messaging refresh). Currently installed:
  `1.11.0+plasma.2-20261015-004612`, running (PID seen as 545802).
- `scripts/install-plasma-local.sh` requires JDK 25: it defaults
  `JAVA_HOME=/usr/lib/jvm/java-25-openjdk`. NEVER build packaging with default Java 17
  (produces a Java-17 runtime that can't load class v69 → instant crash on start).
- User data: `~/.abdm/config/` (`appSettings.json` — NEVER touched by my builds, still
  37 keys, intact; `pageStatesStorage.json` — new schema, Pair-style windowSize etc.).
  Install backups: `~/.local/state/abdm-backups/install-*` (several from this session).
  My manual backup: `/tmp/probe/config-backup/`.
- KWin sources on disk: `~/Code/kwin-upstream` (master-ish, v6.7.90-ish). Used heavily
  for tracing (effects are JS in `src/plugins/*`, C++ in `src/*.cpp`).
- Compose Multiplatform sources: `compose-multiplatform-core` on GitHub
  (`jb-main` branch for v2 API). Local jars in `~/.gradle` cache.

## 2. Original request → clarifications

1. "When the dashboard is fullscreened, user can still drag it from title bar and it
   remains fullscreened." → **FIXED** (drag now restores-then-moves, user-confirmed ✓).
2. "Closing while fullscreened: weird closing animation, looks like minimize first then
   close." → User later clarified: **closes via X button, window is MAXIMIZED (not Meta+F
   fullscreen)**. Symptom now: **"it unmaximizes then disappears"** — STILL OPEN.
3. "Anything else you find on my system" → found + fixed several (geometry corruption,
   broken maximize toggle from fullscreen, wrong restore icon, missing double-click,
   resizer cursors). All verified. See §4.

## 3. What I changed (all in `ab-download-manager-plasma`, unstaged)

New files (`desktop/app-utils/.../window/linux/`):
- `X11Ewmh.kt` — internal object, minimal EWMH via JNA (fails soft everywhere):
  `isFullscreen(xid)`, `setFullscreen(xid, bool)`, `supportsMoveResize()`,
  `sendMoveResize(xid, xRoot, yRoot)` (native `_NET_WM_MOVERESIZE` MOVE),
  `setSkipCloseAnimation(xid, bool)` (`_KDE_NET_WM_SKIP_CLOSE_ANIMATION` CARDINAL),
  `sync()` (XSync round-trip for cross-connection ordering).
- `LinuxWindowState.kt` — `LinuxWindowStateTracker` (per-window; re-reads
  `_NET_WM_STATE_FULLSCREEN` + native `extendedState` on AWT component/state events;
  exposes `isFullscreen`/`extendedState` StateFlows), `LocalWindowIsFullscreen`,
  `isFullscreenNow()` (sync query), `exitFullscreen()`, `skipCloseAnimation()`
  (set+sync), `skipCloseAnimationsForExit()` (all top-level windows, never throws),
  `canEwmhMove()`, `nativeMoveNow()`, `awaitUnfullscreenAndNativeMove()` (suspend:
  exit → poll ≤500ms for !fullscreen while button held → send MOVE), `isMaximizedNow()`
  (any maximized bits), `isFullyMaximizedNow()` (exact MAXIMIZED_BOTH).

Modified:
- `desktop/app-utils/.../window/custom/CustomWindow.kt` (the bulk):
  - `SystemDraggableSection` routing: JBR → slop-gated native move (except fullscreen);
    EWMH-capable Linux + maximized/fullscreen → `NativeMoveArea` (custom slop-gated
    gesture detector; fullscreen uses 3x touch slop); floating → legacy
    `WindowDraggableArea`; off-Linux → legacy always. Double-tap-to-toggle extracted to
    `DoubleTapToToggleMaximize`, active on the whole bar in all branches.
  - Native drag area is the **title content only** (`contentDragModifier` threaded
    through `FrameContent` → `TitleBar.RenderTitleBar` (+Linux/Mac/Windows impls) →
    `CommonRenderTitleBar` → `RenderTitleBarContent`); system buttons are tap-only so
    button clicks can never start drags (hit-test exclusion, not consumption).
  - Maximize toggle exits WM-fullscreen first (sync check at action time).
  - Close (`closeWindow`, used by X button AND WM-close via a `CloseHandler` ref):
    sets skip flag when `isFullscreenNow || isMaximizedNow` (any maximized bits incl.
    tiled); **ignores further toggles once closing starts** (`AtomicBoolean`
    `closeRequested` — against double-click/switch-bounce toggling restore mid-close).
  - Undecorated edge resizers suppressed (`undecoratedResizerThickness = 0.dp`) when
    fullscreen or fully-maximized (tiled keeps resizers); frame border follows.
  - `isWindowMaximized()` also true when WM-fullscreen (restore icon).
- `desktop/app/.../pages/home/HomeWindow.kt`: geometry saver skips WM-fullscreen
  (sync query) and anything with native maximized bits (covers Compose placement-sync
  lag during animated transitions).
- `desktop/app/.../pages/settings/SettingWindow.kt`: same guards; collector moved
  inside `CustomWindow` content (needs `window`).
- `desktop/app/.../AppComponent.kt`: `exitApp()` calls `skipCloseAnimationsForExit()`
  right before `exitProcess(0)`.
- `desktop/app-utils/build.gradle.kts`: `implementation(libs.jna.platform)`.
- `PLASMA-LOCAL.md`: plasma.2 log entries (follow-ups 1–5).

## 4. FIXED and verified (do not regress)

All verified at X11-protocol level (xprop/xwininfo polls) and/or screenshots:
- T1: fullscreen → close → reopen preserves exact floating rect (was: reopened
  maximized at fullscreen size, self-reinforcing).
- T2: maximize-button click in WM-fullscreen exits to exact floating rect (was: no-op).
- T3: double-click maximized titlebar restores (was: nothing — no JBR on this box).
- T4: restore icon shows in fullscreen-from-floating.
- J1: jitter-click X in fullscreen → FULLSCREEN → GONE, no intermediate geometry.
- J2: sub-3x-slop title jitter in fullscreen → nothing.
- J3: 60px title drag in fullscreen → exits to floating.
- Exit path: skip flag present on window ~200ms before death across `gui exit`.
- Maximize-button and double-click toggles re-verified working AFTER the close-guard
  change (guard doesn't break normal toggles).
- User-confirmed: drag-from-fullscreen restores+moves ✓ (notes instant snap — inherent:
  cursor-following leaves no room for the 250ms un-fullscreen animation; almost surely
  matches native Kate behavior).

## 5. THE OPEN BUG

Maximized dashboard + X button (single real user click): window visibly un-maximizes
(shrinks to floating size), THEN disappears. Persists across all fixes so far.

### 5a. Established facts (each verified, not assumed)

- F1. At X11 level the close is clean: maximized geometry held until unmap/destroy; no
  intermediate floating geometry (polls at ~30ms: `2560x1440 maximized → GONE`). The
  unmap→destroy gap exists (~40–300ms); KWin clears `_NET_WM_STATE` on unmap
  (`STATE[]` sample) — invisible bookkeeping.
- F2. The skip flag lands before destroy: `_KDE_NET_WM_SKIP_CLOSE_ANIMATION(CARDINAL)=1`
  caught by poller on both fullscreen and maximized closes (26ms+ before GONE).
- F3. KWin 6.7.5 `scale` effect checks the flag live
  (`slotWindowClosed: if (!window.visible || window.skipsCloseAnimation) return;` —
  confirmed present in v6.7.0 tag AND installed
  `/usr/share/kwin-wayland/effects/scale/contents/code/main.js:130`).
- F4. No other LOADED effect handles window close with an animation (audited every
  loaded effect incl. third-party `better_blur_dx` 2.5.1 sources from paru cache:
  only bookkeeping/cancels). `maximize`/`fullscreen` effects CAN animate deleted
  windows in principle (`EffectWindow::isVisible()` stays true — deleted ≠ hidden),
  BUT no code path resets internal maximize/fullscreen mode during
  `releaseWindow`/`destroyWindow` (read both fully; the `info->setState(clear)` echo
  has no property path back to `setMaximize` — `WinInfo::changeState` handles client
  *messages* only), so their signals shouldn't fire. (This last link is source-read,
  not runtime-proven.)
- F5. Compose v2 disposes directly (`listeners.remove; it.dispose()`), no placement
  restore. AWT dispose = unmap + destroy, no state change.
- F6. Single app process, single dashboard window (zombie theory ruled out). Installed
  build contains the new code (verified classes/methods in the shipped jar).
- F7. Synthetic single/jitter/double clicks all close cleanly at protocol level; only
  human clicks reproduce. Synthetic double-clicks LOSE to dispose (~70ms destroy).
- F8. Reopened window restores maximized when closed maximized — BY DESIGN (upstream
  "remember maximized state"), not a bug. (Persisted `isMaximized=true` is correct if
  closed while maximized.)

### 5b. Dead theories (do not re-chase without new evidence)

- D1. WM-fullscreen invisible to AWT/Compose → FIXED via EWMH tracker (but user meant
  maximized anyway; true-fullscreen path is correct + verified).
- D2. Jitter starting drags on button clicks → FIXED via spatial exclusion (buttons
  outside drag subtree — structural, can't fail) + 3x slop for fullscreen. J1 proves
  button jitter no longer disturbs close.
- D3. Double-click/switch-bounce toggling restore mid-close → GUARDED (`closeRequested`
  AtomicBoolean; toggle ignored post-close). Shipped in latest installed build.
  (Whether it was THE cause is unproven — synthetic doubles couldn't beat dispose —
  but the guard is correct regardless.)
- D4. Quit path (`exitProcess`) lacking flags → FIXED (`skipCloseAnimationsForExit`;
  verified flag present pre-death). (User closes via X, so likely unrelated to their
  report, but correct anyway.)
- D5. Tiled-vs-maximized → flag now covers ANY maximized bits (incl. tiled).
- D6. Maximize-effect-on-deleted — no trigger exists (see F4). Would need the echo→
  setMaximize link that doesn't exist.
- D7. Stale build / zombie process / wrong tree — ruled out (F6, §1).
- D8. `hasActiveFullScreenEffect` gating — unrelated (fullscreen-effect *plugins*,
  not windows).

### 5c. Open possibilities (ranked by my judgment)

- O1. **User's clicks bounce/double-fire AND win the race on their timing.** My
  synthetic doubles lost to dispose (~70ms), but real bounce (5–15ms gaps) or a slower
  dispose under load could let press2's double-tap toggle fire pre-unmap. **The
  close-guard fix for this is already installed** (latest build) — if the user retests
  on the CURRENT installed build (`...-004612`) and it's fixed, this was it. (Possible
  they tested before reinstall finished.)
- O2. **KWin-side maximize animation via a path I missed.** Decisive test: unload
  `maximize` effect, have USER close (they're the only reliable detector), ask
  better/same, reload immediately. (I unloaded/reloaded `scale` and `maximize` before
  via `qdbus org.kde.KWin /Effects org.kde.kwin.Effects.unloadEffect/loadEffect` —
  safe, instant.) If better with maximize unloaded → the effect DOES fire → then find
  the trigger (add `print()` to a shadowed copy at
  `~/.local/share/kwin/effects/maximize/` + journal).
- O3. **The `scale` effect plays despite the flag due to a KWin-side ordering subtlety
  I got wrong.** Decisive test: shadow `scale` effect with logging
  (`print("SCALE-CLOSE", window.windowClass, window.skipsCloseAnimation)`) in
  `slotWindowClosed`, reload, close, read journal. Fully reversible (delete shadow).
- O4. **User's "unmaximize" = the scale animation ITSELF misjudged** (scale 1.0→0.8 on
  a 2400px window IS a big shrink; with InCubic easing it can read as staged). If O2/O3
  show NO effect fires yet they still complain → it's perception of... what, though?
  Without an effect there's no animation at all. Would need fresh eyes + maybe a video
  from the user (OBS is installed with a PipeWire capture scene configured).
- O5. **Something in Decompose/Compose teardown un-maximizes**: e.g., a `requestPlacement`
  from a `DisposableEffect`/observer during unmount, or `SnapDraggableToolbar`'s JBR
  placement-restore block (JBR unsupported here — dead code path, but double-check
  nothing else writes placement on dispose). Could add temp logging to
  `requestPlacement` call sites... they're Compose-internal; instead log
  `extendedState` in a window listener during close (catch a maximize→normal flip
  pre-unmap!). CHEAP and decisive for "real un-maximize vs painted animation":
  poll showed NO geometry change pre-GONE (F1) — wait, that already rules out a REAL
  unmaximize!! A real un-maximize would resize to floating BEFORE unmap (poll would
  catch 1401x727-mapped samples — never seen in ANY poll, incl. J1). SO: the window
  does NOT really unmaximize → the shrink is PAINTED (an effect) → back to O2/O3.
  (Unless polls (~30ms) alias a <30ms real unmaximize — imperceptible anyway.)

### 5d. Recommended next steps

1. Confirm the user tested the CURRENT build (`1.11.0+plasma.2-20261015-004612`,
   `readlink .../current`) — the close-guard only just shipped.
2. If still broken: run the `maximize`-unload user test (O2). 2 minutes, decisive.
3. If not maximize: shadow-`scale` logging (O3).
4. Nuclear option if effects exonerated: capture what they see (ask for a phone video
   or use their OBS); re-examine premise (maybe it's the *reveal*, panels, or focus
   change they read as animation).

## 6. Code map (my changes, `ab-download-manager-plasma`, unstaged)

- `desktop/app-utils/.../window/linux/X11Ewmh.kt` (NEW): EWMH/JNA plumbing.
- `desktop/app-utils/.../window/linux/LinuxWindowState.kt` (NEW): tracker, locals,
  actions. NOTE: `Window.xidOrNull()` = `ComposeWindow.windowHandle`, proven == frame
  ID (T2 + `_NET_WM_STATE` reads behave frame-correctly).
- `desktop/app-utils/.../window/custom/CustomWindow.kt`: drag routing, NativeMoveArea
  (manual slop state machine — see header comment), toggle/close wraps, close-guard,
  resizer suppression, CompositionLocals.
- `.../titlebar/{TitleBar,TitleBarShared}.kt` + `linux|mac|windows/*TitleBar.kt`:
  `contentDragModifier` plumbing (interface default; overrides omit default per Kotlin
  rules).
- `desktop/app/.../pages/{home/HomeWindow,settings/SettingWindow}.kt`: saver guards.
- `desktop/app/.../AppComponent.kt`: `skipCloseAnimationsForExit()` in `exitApp()`.
- `desktop/app-utils/build.gradle.kts`: `+ libs.jna.platform`.
- `PLASMA-LOCAL.md`: running log (follow-ups 1–5).

## 7. Test tools (`/tmp/probe/`)

- `xclick.py <winid> <x> <y>` — synthetic tap on canvas child (proven to reach Compose).
- `xjitter.py <winid> <x> <y> "x1,y1,..." [gap_ms]` — press + motions + release.
- `xdblclick.py <winid> <x> <y> [gap_s]` — two taps (default 0.12s).
- `poll.sh <winid> <log> <secs>` — 30ms geometry+state+mapped sampler.
- `flagpoll.sh` — same for `_KDE_NET_WM_SKIP_CLOSE_ANIMATION`.
- `xmoveresize.py` — precise XMoveResizeWindow (X coords!).
- `ProbeWinState.java` — AWT probe (extendedState visibility; processes cleaned up).
- Close-button rel coords (fullscreen/maximized 2400-wide): `(2366, 26)`; 2560-wide:
  `(2526, 26)`; maximize ≈ 40px left of close. Titlebar ~51px tall. Canvas ID via
  `xwininfo -name ... -tree | grep Canvas`.

## 8. Gotchas / hard-won notes

- xdotool pointer control is DEAD on this setup (moves ignored; verified). Use synthetic
  XSendEvent (scripts above) or ask the user.
- `pkill -f "X"` kills your own shell if the pattern appears in its cmdline — always use
  bracket trick (`"X[Y]"`).
- `cmd1 && cmd2 &` backgrounds the WHOLE chain (lost `cd`s that way); use `;` before `&`.
- `spectacle -b` rapid-fire can wedges captures (black frames); single shots + sleeps.
  Screen may LOCK (then captures are black) — check
  `qdbus org.kde.Screensaver /ScreenSaver ...GetActive`.
- KWin effect slow-mo: `[Effect-scale] Duration=N` in kwinrc + `reconfigureEffect`
  works (maximize/fullscreen durations are hardcoded 250ms, not configurable). MUST
  restore afterwards (done — kwinrc backup at `/tmp/probe/kwinrc.backup`; verify with
  `grep -c Effect-scale ~/.config/kwinrc` → 0).
- qdbus effect control: `org.kde.KWin /Effects org.kde.kwin.Effects.{loadEffect,
  unloadEffect,isEffectLoaded,reconfigureEffect}`.
- Coordinate spaces: xwininfo/X11 = truth (X px). `wmctrl -l -G` positions are
  KWin-scaled (≈2x on the right monitor!). Sizes agree. Screenshot px = X px × 1.6.
- Compose version here is 1.12.1 (v2 window API); upstream master (jb-main) matches.
- The stale `~/Code/ab-download-manager` tree: my scratch port + a `createReleaseDistributable`
  output live there; nothing ships from it. `PLASMA-LOCAL.md` there says "left unchanged"
  — I did dirty it (my files); disclose, don't auto-revert (would destroy pre-existing
  uncommitted work layered in the same files).
- Install script exits the RUNNING app; relaunch manually after (+ reopen dashboard).
  App runs `--background` + dashboard on demand; single-instance server mediates.
- `~/.abdm/config/pageStatesStorage.json` uses the NEW Pair schema in this tree — don't
  let an old-schema build rewrite it (I did this once from the stale tree; restored
  from backups + context; current file state is healthy).
- User is terse but responsive; local time ~01:00 at handoff — likely back later.
  Screen was locked at last check.

## 9. Suggested opening move for next agent

1. `readlink .../current` (expect `...-004612`), confirm app running, dashboard state.
2. Ask user to retest maximized-X close on THIS build (close-guard is new since their
   last test) — or if they're away, proceed to the maximize-unload experiment solo
   (it needs their eyes; screenshots keep missing).
3. If still broken with guard confirmed active → O2 (maximize unload user test) →
   O3 (shadow-scale logging) in that order.
