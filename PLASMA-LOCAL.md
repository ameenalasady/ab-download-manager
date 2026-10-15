# Local KDE Plasma / Wayland build

## Origin and installation

- Original modified checkout: `~/Code/ab-download-manager`, based on `v1.10.2`. Left unchanged.
- Saved original patch: `~/Downloads/abdm-linux-wayland-fixes.patch`.
- This worktree: `~/Code/ab-download-manager-plasma`, branch `local/plasma-wayland-1.11.0`, based on upstream `v1.11.0` (`5b2cffe4`).
- Installed app version: `1.11.0+plasma.2` (release `1.11.0+plasma.2-20261015-022633`).
- User-local installation: `~/.local/opt/ab-download-manager-plasma/current`, pointing at a versioned release directory.
- Main/CLI/native-host wrappers: `~/.local/bin/ABDownloadManager*`.
- KDE launcher override: `~/.local/share/applications/com.abdownloadmanager.desktop`.
- Autostart: `~/.config/autostart/com.abdownloadmanager.desktop`.
- Native messaging manifests regenerated using the installed local CLI. Their paths point at the versioned release (jpackage resolves symlinks).
- Stock pacman package `ab-download-manager-bin 1.11.0-1` remains unchanged and available as a fallback.

## Audit findings

Retained: null-safe pointer lookup, multi-monitor work-area placement, restoration of reachable geometry, system/fractional UI scaling and font-size support, Linux window activation workaround, Plasma 6 proxy settings, nonblocking external-app launches, file/folder-opening corrections, Linux notification sound fallbacks, power-action support, and settings flush on exit.

Corrected during the port:

1. KDE decoration-section matching previously used `endsWith("kdecoration")`, which failed for this machine's `[org.kde.kdecoration2]`. It now accepts the numbered sections and honors `XDG_CONFIG_HOME`.
2. The title-bar settings-command timeout previously followed blocking `readText()`, making it ineffective for hung commands. Timeout now precedes reading the small settings output.
3. Ported all affected windows to Compose's v2 window API, guarding access until initialization and using request-based state changes.
4. Removed the redundant initial resize of the completed-download dialog. Used upstream minimum-size handling instead of repeating AWT size hints on recomposition.
5. Retained upstream fixes already present for external-library and per-host-settings window scaling rather than duplicating them.
6. Serialize debounced persistence and shutdown flushes. A delayed old snapshot cannot overwrite the current state. Preserve cancellation and log flush errors.
7. Copy live home/settings component geometry into storage before flushing; otherwise their own debounce can leave the storage snapshot behind.
8. Reject nonfinite/out-of-range UI-scale overrides. Recognize `plasma` as well as `kde` for font detection.
9. Restrict the always-on-top activation workaround to Linux.
10. Disable launcher startup notifications when no completion hook exists; enabling them is not itself a fix for a lingering busy cursor.
11. Added the null-safe pointer fix to the newer location-selector dialog as well.

The existing KWin remember-window-positions script blacklist entry for `com-abdownloadmanager-desktop-AppKt` is still present and was not modified. This prevents the script from competing with the app's own restoration.

The user-local main wrapper preserves the Arch launcher's `SKIKO_RENDER_API=SOFTWARE` default. An explicitly set value overrides it.

## Verification (original plasma.1 port)

Successful full release build with Java 25:

```sh
JAVA_HOME=/usr/lib/jvm/java-25-openjdk ./gradlew \
  -PlocalAppVersion=1.11.0+plasma.1 \
  :desktop:app-utils:test :desktop:app:createReleaseDistributable
```

Six regression tests passed, with zero failures/errors:

- KDE decoration versions/default right-hand controls
- Left-hand controls
- Missing decoration section
- Fractional window-size round trips (including unscaling height)
- Hung command timeout
- Invalid scale overrides

`git diff --check` and installer `bash -n` passed. Desktop files validated (only the pre-existing multiple-main-category menu hint remains). The installed GUI starts successfully, exposes integration port 15151, and a second launch leaves one GUI process. WM_CLASS matches the launcher and KWin blacklist. Main/CLI versions and native-host manifest paths were checked.

Not automated: visual correctness on every monitor, focus behavior under every KWin policy, browser-extension round trips, audio playback, and suspend/hibernate/shutdown. Power actions were intentionally not invoked. Scaling is detected at startup and uses a global correction; mixed-DPI screens/hotplug still need manual checks. AWT is running through XWayland, not native Wayland.

## Rebuild / reinstall

```sh
cd ~/Code/ab-download-manager-plasma
JAVA_HOME=/usr/lib/jvm/java-25-openjdk ABDM_LOCAL_VERSION=1.11.0+plasma.2 \
  scripts/install-plasma-local.sh
```

The script defaults to plasma.2 and Java 25. It builds/tests, requests a graceful exit of any running app, backs up data/integration files, installs a new versioned release, and refreshes KDE/browser integration and an already opted-in owned KWin companion. Do not package with Java 17. Launch afterwards through the normal KDE entry with `kstart --application com.abdownloadmanager`.

Package updates do not overwrite the user-local build. Do not install an upstream in-app update over it if you want to retain these patches. To adopt another upstream version, port/audit the patch first and update the local version marker; this is not an automatic patch updater.

## Backups and rollback

Original source/data backup: `~/.local/state/abdm-backups/20261012-182330/`.
Pre-install integration/data backup: `~/.local/state/abdm-backups/install-20261012-183333/`.

To return to the stock package (without rolling back download data):

```sh
ABDownloadManagerCli gui exit
rm ~/.local/bin/ABDownloadManager ~/.local/bin/ABDownloadManagerCli \
   ~/.local/bin/ABDownloadManagerNativeMessagingHost \
   ~/.local/share/applications/com.abdownloadmanager.desktop
cp ~/.local/state/abdm-backups/install-20261012-183333/home/alasady/.config/autostart/com.abdownloadmanager.desktop \
   ~/.config/autostart/com.abdownloadmanager.desktop
/usr/bin/ABDownloadManagerCli native-messaging install
kbuildsycoca6 --noincremental
/usr/bin/ABDownloadManager
```

The local release directory can remain for recovery. Do not restore old `.abdm` data unless you intentionally want to revert intervening settings/download state.

## Current plasma.2 window behavior

- Close requests go directly to the caller. They neither unmap the window nor latch a
  closing flag, so a canceled/deferred close leaves maximize/fullscreen controls usable.
- On actual disposal, an outer hook registered AFTER `Window` unmaps enlarged windows and
  XSyncs before AWT can issue its unwanted restore. Normal KWin close animation is retained;
  no skip-close-animation flags are written.
- Native restore-on-drag, deliberate fullscreen drag slop, title-content-only drag hit
  testing, native state tracking, geometry-saving guards, and platform fallbacks are retained.
- The intentional user-local KWin `maximize` companion applies a 1e-6 size tolerance ONLY
  to ABDM's window class. It is not a temporary logging override; keep its ownership/hash
  checks and explicit opt-in. See follow-up 8 below for refresh/removal instructions.
- Cleanup removed the obsolete close guard/callback publishing bridge and unused
  `isFullyMaximizedNow()`. The unused `getCurrentWindowSize()` and its exclusive imports
  were also removed; that helper predated this port in upstream v1.11.0.

## plasma.2 implementation history (2026-10-14 onward)

The entries below are chronological investigation notes, not current instructions. Early
close theories, skip flags, and the defensive close guard were superseded; use the current
summary above. The confirmed disposal fix and intentional KWin companion remain in place.

### Initial window-manager fullscreen implementation (2026-10-14)

AWT only tracks the iconic/maximized bits of `_NET_WM_STATE`, so a KWin fullscreen was
invisible: compose kept reporting `Floating` at the fullscreen size. That one blind spot caused
all of these at once - dragging the fullscreen title bar did nothing, the maximize button and
title-bar double-click misbehaved, the restore icon never showed, and the fullscreen size was
persisted as the floating geometry (reopening maximized, self-reinforcing).

- New `desktop/app-utils/.../window/linux/`: minimal EWMH client (`X11Ewmh`, JNA, fails soft)
  plus a per-window tracker that re-reads `_NET_WM_STATE_FULLSCREEN` and the native maximized
  bits on AWT geometry/state events. Exposes `LocalWindowIsFullscreen`, `isFullscreenNow()`
  (synchronous, for actions) and `exitFullscreen()` (EWMH request, KWin restores on its own).
- `CustomWindow`: title-bar drag disabled while fullscreen (and while maximized for the
  setLocation fallback, where the WM ignores moves; the native JBR move keeps its
  un-maximize-into-drag); double-click to toggle added to the fallback path, which previously
  had none; maximize toggle exits a WM fullscreen instead of flipping ignored maximized bits;
  close hands the fullscreen state back before disposing (KWin otherwise animates an
  un-fullscreen followed by the close - verify visually); undecorated edge resizers put away
  while fully maximized/fullscreen (tiled windows keep theirs); frame border follows suit;
  maximize/restore icon treats fullscreen as maximized.
- `HomeWindow`/`SettingWindow`: geometry saver skips WM-fullscreen windows (sync query, no
  reliance on cached state) and anything with native maximized bits, covering compose's
  placement-sync lag during animated transitions.
- Install with `ABDM_LOCAL_VERSION=1.11.0+plasma.2 scripts/install-plasma-local.sh`.
- Protocol-verified on KWin/XWayland: fullscreen-close-reopen preserves geometry; the
  maximize button exits fullscreen to the exact floating rect; double-click restores a
  maximized window; the restore icon shows in fullscreen-from-floating. Still needs a human:
  drag feel, the close-animation change, resizer cursors, tiled-window behavior.

Follow-up (same version, still `1.11.0+plasma.2`):

- Title-bar drag of maximized/tiled/fullscreen windows now restores-then-moves like native
  apps, via `_NET_WM_MOVERESIZE` (`X11Ewmh.sendMoveResize`, gated on `_NET_SUPPORTED`).
  Fullscreen is left first and the move handed over once KWin is done, aborting if the
  button goes up meanwhile. Moves start past the touch slop, so bare clicks - notably on
  the title-bar buttons - never start (and un-maximize into) a move; the JBR branch got the
  same slop gating. Off linux the legacy behavior is untouched.
- Close-from-fullscreen no longer pre-releases the fullscreen (that just made the
  un-fullscreen-then-close two-step explicit). Instead it sets KWin's
  `_KDE_NET_WM_SKIP_CLOSE_ANIMATION` and syncs before disposing, so the `scale` effect
  (scale-to-0.8 + fade, the perceived shrink) is skipped and the window vanishes in place.
  Verified: the property lands over the shipped code path, detection reads false/true
  correctly, `_NET_SUPPORTED` advertises move/resize. Needs a human: drag feel from every
  state, the new close, slop behavior on clicks.

Follow-up 2 (same version): the close still animated because real clicks jitter past the
  drag slop: the press on X started the fullscreen-drag path, which visibly un-fullscreened
  before the tap completed - and by close time the window was no longer fullscreen, so the
  skip flag never armed. Fixes: the native drag area is now the title content only (system
  buttons are tap-only, like native title bars; threaded as `contentDragModifier` through
  the title-bar interface), and leaving a fullscreen waits for 3x touch slop instead of 1x.
  Verified: jitter-click on X closes straight from fullscreen with no intermediate state;
  sub-3x jitter on the title does nothing; a 60px drag exits fullscreen. Temp close-path
  logging was added to prove the flag armed (`set=true verify=true`) and removed again.
  Note: the instant fullscreen->restored snap at drag start is inherent - following the
  cursor leaves no room to play the 250ms un-fullscreen animation first.

Follow-up 3 (same version): quitting (`exitApp` -> `exitProcess`) destroyed every window
  with no skip flags, so a quit from fullscreen played the scale animation (on all windows
  at once). `skipCloseAnimationsForExit()` now flags all top-level windows right before
  dying. Verified: flag present ~200ms before death across the exit.

Follow-up 4 (same version): the reported closes are maximized, not Meta+F fullscreen - and
  the maximized close plays the same shrink-then-close scale animation, with no app-side
  two-step involved (verified: Compose v2 disposes directly, no placement restore). The
  close now also skips the animation when fully maximized (tiled and floating closes keep
  the standard fade). Verified: flag lands before destroy on the maximized path.

Follow-up 5 (same version): guarded against a possible second press racing the close
  (a double-click or bouncing switch). The toggle, including fullscreen exit, is ignored
  once a close starts (`AtomicBoolean` in `CustomWindow`). Also extended the skip to
  half-maximized (tiled) closes. Verified: normal button/double-click toggles still work;
  flag lands pre-destroy. This was a defensive mitigation, NOT the established cause:
  the user still reproduced the animation with this guard installed. See follow-up 6.

Follow-up 6 (2026-10-15, same version): **root cause found and disposal fix installed**.

- Human A/B test: stock effects still restored-then-disappeared; unloading ONLY `maximize`
  made the same close disappear in place. Restored the effect immediately afterward.
- Temporary, logging-only shadow of the installed `maximize` effect caught a genuine
  `ABOUT(false,false)` -> floating `GEOMETRY` -> `CHANGED(false,false)` -> `CLOSED`
  sequence in ~1 ms, with `skipsCloseAnimation=true` throughout. The effect retained the
  closing window for its ~250 ms restore animation. Earlier ~30 ms X11 polls missed this
  brief transition; the skip flag only suppresses `scale`, not an already-started restore.
- Cause: plain OpenJDK 25's `sun.awt.X11.XFramePeer.setVisible(false)` calls
  `XWM.setExtendedState(state & ~Frame.MAXIMIZED_BOTH)` BEFORE delegating to the unmap.
  Confirmed in the shipped runtime's bytecode (`/tmp/probe/XFramePeer.javap`). This is
  AWT hide/dispose behavior, not a title-bar gesture, Compose placement request, or
  KWin's state cleanup after unmapping.
- `X11Ewmh.unmap` + `prepareForWindowDisposal`: when an actually-disposing window is
  maximized/tiled/fullscreen, arm the skip flag and unmap its native client first, then
  XSync before AWT's own X connection sends the restore. KWin releases the window at
  its original geometry, before that stale restore request can animate it.
- `CustomWindow` registers an outer `DisposableEffect` AFTER `Window`, so reverse
  disposal ordering runs this preparation before Compose/AWT teardown. A content-level
  effect would be too late: `ComposeWindow.dispose()` calls `JFrame.dispose()` before
  disposing content. Do not withdraw in `onCloseRequest`: callers may veto that request.
- Native-unmap is disposal-only; ordinary restore/maximize/drag paths are untouched.
  Floating closes retain their normal animation. No permanent KWin override/config edit.
- Installed via the normal JDK 25 installer as `1.11.0+plasma.2-20261015-011542`.
  KWin runtime verification: maximized X-close, maximized WM-close, and fullscreen
  X-close now emit `CLOSED`/`DELETED` at maximized/fullscreen geometry, with NO restore
  signal or maximize-animation IDs. Floating restore still emits the expected maximize
  effect signals; floating close has `skipClose=false` and retains its normal animation.
- Seven tests pass (the original six plus a headless disposal-order regression test).
  Post-fix maximize-button and titlebar-double-click round trips preserve the exact
  floating rectangle `(685,345 1401x727)`; the maximize button exits WM-fullscreen to
  the underlying maximized state. Dashboard left open maximized for the visual retest.
  Temporary tracing shadows removed; stock `maximize` and `scale` loaded.
  User confirmed maximized closes disappeared, but clarified this is NOT the desired
  result: the normal close animation should run from maximized geometry. See follow-up 7.
- Evidence: `/tmp/probe/close-effect-ab-20261015-010956/` (human A/B),
  `/tmp/probe/maximize-trace-20261015-011225/` (old restore-on-close),
  `/tmp/probe/maximize-trace-20261015-011639/` (fixed X-close), `011650` (WM-close),
  `011656` (fullscreen), `011702` (floating). Driver: `/tmp/probe/trace-maximize-close.py`
  (`ABDM_PID=<current-pid>`, optional `ABDM_TEST_MODE=maximized-wmclose|fullscreen-click|floating-click`).

Follow-up 7 (2026-10-15 ~01:26, same version): **animate the close from its actual size**.

- Requirement clarified: a maximized window should play the normal close effect directly
  from maximized geometry, not restore first AND not simply disappear. The previous
  skip-animation workaround overcorrected; it was not the intended final behavior.
- Retained native unmap + XSync in the outer disposal hook, which prevents AWT's unwanted
  restore. Removed all skip-close-animation writes/helpers (including the quit workaround)
  so KWin owns the normal close animation for maximized, tiled, fullscreen, and floating
  windows. The close guard remains as a defensive gesture guard, not an animation fix.
- Installed `1.11.0+plasma.2-20261015-012340` via the JDK 25 installer; running PID
  `591630`. Install backup: `~/.local/state/abdm-backups/install-20261015-012337/`.
- Temporarily instrumented BOTH stock `maximize` and `scale`, without changing behavior:
  maximized X-close and WM-close emit no restore signals; `scale.CLOSE-ANIMATED` starts
  at the unchanged maximized geometry with two animation IDs and `skipClose=false`.
  Deleted-window lifetime is ~220 ms, ending with the scale/opacity animations.
  Fullscreen, floating close, and maximized quit show the same correct effect behavior.
- Evidence: `/tmp/probe/animated-close-20261015-012435/` (maximized X-close, 223 ms),
  `012449` (WM-close, 221 ms), `012453` (fullscreen, 214 ms), `012459` (floating,
  222 ms), `012505` (quit, 226 ms). Driver: `/tmp/probe/trace-animated-close.py` with
  `ABDM_PID=<pid>` and optional `ABDM_TEST_MODE=maximized-wmclose|fullscreen-click|floating-click|quit`.
- All seven tests pass. Temporary effect shadows removed, stock maximize and scale loaded.
  appSettings.json is byte-identical to the install backup. Dashboard reopened maximized
  for the human visual retest. User confirmed: "omg this is perfect" — closing behavior
  is now visually verified. Keep it intact.

Follow-up 8 (2026-10-15 ~01:38): **restore-on-drag animation / KWin roundoff**.

- New user nitpick: dragging the maximized titlebar restored instantly rather than
  animating. Native move handoff was already correct; do not add an artificial delay.
- Real Wayland input via the running `ydotoold` reproduced it. KWin started its restore
  effect, then canceled it 25 ms later: `maximize/main.js` treats any exact width/height
  inequality as a resize. At 125% scale a position-only move changed the reported width
  from `1120.8000000000002` to `1120.8`, so numerical noise killed the animation.
- Tested an ABDM-only `1e-6` logical-pixel comparison tolerance in the two size guards.
  Same real drag now runs to animation end (~276 ms) with no cancel. Real resizes still
  cancel; all other applications retain stock exact comparisons. No app drag, close,
  scaling, or persisted-size logic was changed for this follow-up.
- Installed a no-logging user-local companion copy of the stock `maximize` effect:
  `~/.local/share/kwin-wayland/effects/maximize/`. It changes only the two comparisons
  for ABDM's exact WM_CLASS. The `scale` close effect remains stock.
- Reproduce / refresh after KWin updates:
  `python3 scripts/plasma-maximize-workaround.py install`.
  Undo and immediately reload stock:
  `python3 scripts/plasma-maximize-workaround.py remove`.
  The helper records source/generated hashes, refuses unrelated or edited overrides,
  and refuses unknown upstream layouts. This is opt-in; the regular app installer
  refreshes it only when its ownership marker already exists. No package files/kwinrc
  are changed. Remove the workaround if upstream changes need review or fix this bug.
- Tests: six Python/JavaScript regression tests for numerical-noise vs real resize,
  other-app isolation, ownership, refresh/remove, and upstream-layout guards; all pass.
  Existing seven Kotlin tests still pass. `node --check`, installer syntax, and
  `git diff --check` pass.
- Evidence: `/tmp/probe/title-drag-20261015-013038/` (25 ms cancellation),
  `...-013214/` (276 ms completed restore). Driver `/tmp/probe/trace-title-drag.py`
  supports `ABDM_FUZZY_MAXIMIZE=1 ABDM_SKIP_KONSOLE=1` but refuses an existing override;
  do not run it over the installed companion without preserving that copy.
- Confirmed close behavior unchanged with the companion loaded: normal scale/fade
  at maximized geometry, no restore, ~222 ms. Evidence:
  `/tmp/probe/animated-close-20261015-013509/`; preservation-aware driver:
  `/tmp/probe/trace-animated-close-preserve.py` (`ABDM_PID=<pid>`).
- App remains release `1.11.0+plasma.2-20261015-012340` (no binary rebuild needed).
  User approved the result and requested installation as the normal desktop launcher.

Final desktop install (2026-10-15 ~01:40):

- Ran `JAVA_HOME=/usr/lib/jvm/java-25-openjdk ABDM_LOCAL_VERSION=1.11.0+plasma.2
  scripts/install-plasma-local.sh` successfully. Current release:
  `1.11.0+plasma.2-20261015-013921`; backup:
  `~/.local/state/abdm-backups/install-20261015-013918/`.
- Normal KDE entry `~/.local/share/applications/com.abdownloadmanager.desktop` uses
  `~/.local/bin/ABDownloadManager` -> `current`. Its matching desktop ID overrides the
  stock system entry without editing the package or adding a duplicate application.
  Refreshed KDE cache, CLI/native-host/browser manifests, and autostart integration.
- Existing opted-in KWin companion was successfully regenerated/reloaded by the installer;
  no logging, ABDM-only tolerance intact, stock scale effect still loaded.
- Launched using `kstart --application com.abdownloadmanager`, not the wrapper directly:
  PID `606244` resolves to the new release binary. CLI reports `1.11.0+plasma.2`.
- `pacman -Qkk ab-download-manager-bin`: 412 files, 0 altered. appSettings.json remained
  byte-identical to the install backup. Desktop validation passes (only the pre-existing
  category hint); `git diff --check` passes. Nothing committed or staged.

Cleanup after read-only audit (2026-10-15 ~02:26):

- Removed the defensive close-request latch and content-to-outer close callback bridge.
  WM and titlebar close requests now call the supplied callback directly. The renamed
  `WindowDisposalHandle` still publishes the native window to the outer disposal hook;
  native unmap/XSync, effect order, drag routing, and geometry guards are unchanged.
- Removed unused `isFullyMaximizedNow()` and upstream's unused `getCurrentWindowSize()`
  with its exclusive AWT component imports. Updated installer default to plasma.2 and
  the primary rebuild recipe to explicit Java 25/plasma.2. No companion code changes.
- Seven Kotlin tests and all six Python/JavaScript tests passed, none skipped. Release
  rebuild/install, installer syntax, and `git diff --check` passed. Staged plasma.1 diff
  hash remains `926f0ea202579a70673e43841f013e14cf91172a26057a562928dab28b7093e8`.
- Installed `1.11.0+plasma.2-20261015-022633`; backup:
  `~/.local/state/abdm-backups/install-20261015-022630/`. Relaunched via the normal KDE
  entry; CLI reports plasma.2. Owned companion refreshed successfully, ownership/hashes
  validated, and both maximize/scale effects are loaded.
- Interactive close/drag animations were not re-instrumented for this cleanup. Their
  confirmed implementation remains intact; visual evidence above predates this cleanup.
  Companion transactional-write hardening and partial-maximize drag coverage remain
  separate follow-ups, not changes made here. Nothing committed or staged.
