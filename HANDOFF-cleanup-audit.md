# Handoff: audit redundant / inert Plasma changes

## Task from the user

All window-behavior fixes are working and the user is happy. They now request a handoff
to **check for redundant changes from prior attempts or code that does nothing**.

Start with a read-only audit and report findings, distinguishing proven unused code,
obsolete mitigations, intentional portability/fallback code, and unproven suspicions.
Do not turn this into another animation investigation or a broad refactor. No cleanup
has been applied as part of preparing this handoff.

## 1. Repository and safety constraints

- Work ONLY in `/home/alasady/Code/ab-download-manager-plasma`.
- Branch: `local/plasma-wayland-1.11.0`.
- HEAD / upstream v1.11.0: `5b2cffe4765bc66f5ab82f36bd878714bdea3fd7`.
- `/home/alasady/Code/ab-download-manager` is STALE. The harness initially starts there;
  explicitly `cd` to the real tree in every bash call. Do not audit/build/clean the stale
  tree or try to revert its old overlapping changes.
- User's staged changes are the older plasma.1 port; recent fixes are UNSTAGED on top,
  plus new untracked files. **Do not commit, stage, reset, stash, or git-clean anything.**
- Snapshot of the staged diff at handoff:

  ```sh
  git diff --cached --binary | sha256sum
  # 926f0ea202579a70673e43841f013e14cf91172a26057a562928dab28b7093e8  -
  ```

  Keep the index intact even if cleanup is later approved. Check for external user
  changes before assuming this hash must remain the same.
- Distinguish layers:
  - `git diff`: recent unstaged fixes relative to the staged baseline.
  - `git diff --cached`: older, much broader plasma.1 work.
  - `git diff HEAD`: combined changes, NOT exclusively this investigation's work.
  - New untracked Kotlin/Python files do not appear in ordinary `git diff`.

## 2. Installed / running state (already finalized)

- Version: `1.11.0+plasma.2`.
- Current release: `~/.local/opt/ab-download-manager-plasma/releases/1.11.0+plasma.2-20261015-013921`.
- `current` symlink points there. Running PID last seen: `606244`, launched via the
  actual KDE desktop entry, not a Gradle/dev run.
- Desktop override: `~/.local/share/applications/com.abdownloadmanager.desktop`.
  Exec -> `~/.local/bin/ABDownloadManager` -> `current/bin/ABDownloadManager`.
  Same desktop ID as the system entry; no duplicate launcher is needed.
- Autostart, CLI, browser/native messaging, KDE cache refreshed by the final installer.
- Latest backup: `~/.local/state/abdm-backups/install-20261015-013918/`.
- Stock pacman package untouched: `pacman -Qkk ab-download-manager-bin` reported
  412 files, 0 altered. appSettings.json was byte-identical to the install backup.
- User confirmed the proper close animation and approved the animated titlebar drag,
  then explicitly requested installing this result as the normal desktop launcher.

## 3. Essential working fixes: don't remove as apparent no-ops

### A. Close: native unmap BEFORE AWT disposal; normal animation retained

OpenJDK 25 `sun.awt.X11.XFramePeer.setVisible(false)` clears maximized bits BEFORE it
unmaps the window. That starts a real KWin restore animation just before closing.
Earlier 30 ms X11 geometry polling missed the ~1 ms transition, which led to many
incorrect theories. Runtime KWin logging and JDK bytecode proved the actual cause.

Current solution:
- `X11Ewmh.unmap(xid)` + XSync from `prepareForWindowDisposal(window)` for enlarged
  windows, so KWin withdraws them before AWT sends its stale restore request.
- An OUTER `DisposableEffect` AFTER `Window` in `CustomWindow` runs before Window's
  own disposal (reverse effect order). A content-level onDispose is too late because
  `ComposeWindow.dispose()` calls `JFrame.dispose()` before disposing content.
- The window reference is published from content through `SideEffect`.
- Do NOT unmap on a mere close request: a caller may veto/defer it.
- **No `_KDE_NET_WM_SKIP_CLOSE_ANIMATION` writes remain.** The user specifically wants
  the normal scale/fade from maximized geometry, NOT instant disappearance.
- `AppComponent.kt` now has NO unstaged changes: the attempted skip-all-on-exit import
  and call have already been removed. Its remaining staged work belongs to plasma.1.

Native withdrawal, the round trip, and the unusual outer disposal-hook position are
functional, not redundant. Preserve the lifecycle contract if restructuring references.

### B. Restore-on-drag: native EWMH + a real KWin rounding workaround

- Native `_NET_WM_MOVERESIZE` restores/moves maximized windows correctly.
- True WM-fullscreen is exited first, then movement is handed to KWin while held.
- Title-content-only drag hit testing keeps system buttons outside the drag subtree.
- Geometry saving ignores WM-fullscreen and native maximized bits, avoiding persisted
  fullscreen-size corruption and Compose synchronization lag.
- Fullscreen detection is necessary: plain AWT does not track `_NET_WM_STATE_FULLSCREEN`.

The missing drag animation was NOT caused by the native handoff. KWin's `maximize`
effect began the animation, then canceled it 25 ms later because exact equality treated
`1120.8000000000002` vs `1120.8` as a real resize during a position-only move.

An intentional user-local companion is now installed at:
`~/.local/share/kwin-wayland/effects/maximize/`.

- It copies the stock effect and uses a 1e-6 logical-pixel size tolerance ONLY for
  `com-abdownloadmanager-desktop-AppKt com-abdownloadmanager-desktop-AppKt`.
- Other applications keep stock exact comparisons; real resizes still cancel.
- Restoring during a real drag now completes (~276 ms instead of canceling at 25 ms).
- `scale` remains stock; close reverified with companion: ~222 ms scale/fade at unchanged
  maximized geometry, no restore signal.
- Marker `.abdm-maximize-workaround.json` records ownership/source/generated hashes.
  **This is NOT a temporary logging shadow. Do not delete it during cleanup.**
- Helper: `python3 scripts/plasma-maximize-workaround.py install|remove`.
- App installer refreshes only an already opted-in owned companion. This conditional
  branch actually ran successfully in the final install; it is not dead code.
- Refresh after KWin updates; unknown upstream layouts or edited/unrelated overrides
  are refused. This is a compositor workaround, not compiled into the app binary.

Unsupported on this host does not mean dead in the project: bundled runtime is plain
Arch OpenJDK 25 (no JBR decoration/move support), but JBR / Windows / macOS fallback
branches are meaningful elsewhere. Preserve portability unless scope is explicitly narrowed.

## 4. High-value audit candidates (NOT cleanup conclusions yet)

Use `desktop/app-utils/src/main/kotlin/com/abdownloadmanager/desktop/window/` as prefix.

1. **`custom/CustomWindow.kt`: `closeRequested: AtomicBoolean`.**
   Added for an unproven bounce/double-click theory. It did NOT fix the reported close
   problem; AWT disposal was the real cause. Assess whether the guard still protects a
   real race. It is set on every close request and never reset if a close is vetoed or
   deferred; this could permanently disable maximize/fullscreen toggles on a surviving
   window. Trace actual callback lifetimes before proposing removal or a reset policy.

2. **`CloseHandler.handler` / `LaunchedEffect(closeWindow)` publishing bridge.**
   Originally needed window-scoped close-animation flag logic; that logic is gone.
   Current callback only sets the guard and calls the provided close request. The lambda
   is recreated, potentially restarting its LaunchedEffect on recomposition. Consider
   whether the callback can be hoisted or published more directly. The native WINDOW
   reference in this holder is still essential to pre-AWT disposal. Do not delete the
   holder wholesale just because its callback half can potentially be simplified.

3. **`linux/LinuxWindowState.kt`: `Window.isFullyMaximizedNow()`.**
   Repository search currently finds only its definition. Suppression uses a reactive
   extended-state bitmask inline instead. Likely removable unused helper; verify all
   source sets and usages. Do not replace reactive state with a non-observed getter.

4. **`CustomWindow.kt`: private `getCurrentWindowSize()` + its AWT component imports.**
   Repository search currently finds only the definition. ComponentAdapter/ComponentEvent
   in this file are used only by it. This helper ALREADY EXISTS in upstream HEAD; it is
   pre-existing dead-code cleanup, not a failed-attempt remnant. Report provenance.

5. **Overlapping gesture paths / titlebar plumbing / fullscreen slop.**
   Check for unnecessary wrappers or modifiers, but distinguish button spatial exclusion,
   double-click handling, native-vs-legacy routing, JBR portability, and intentional
   fullscreen gesture thresholds. Don't remove whole branches merely because this host
   currently takes one path. Audit partial-maximize/tiled behavior rather than assuming
   full-maximize and quick tiling have identical state semantics.

6. **Installer/docs version drift and stale explanatory text.**
   `scripts/install-plasma-local.sh` still defaults to `1.11.0+plasma.1`; all recent
   installs explicitly set `ABDM_LOCAL_VERSION=1.11.0+plasma.2`. The early rebuild recipe
   in `PLASMA-LOCAL.md` omits this override, while later sections are correct. Flag this
   as outdated metadata/instructions, not a native-window bug. Historical follow-ups
   describe superseded theories and flags: keep a clear current-state summary rather
   than interpreting historical prose as current implementation.

7. **Companion helper robustness vs complexity.**
   Review only with its purpose in mind: ownership checks prevent destructive overwrite;
   exact source-fragment guards prevent blindly patching unknown KWin versions; the
   app-class conditional prevents changing other apps. Those checks are intentional.
   Potential robustness gaps or redundant safeguards can be reported separately from
   actual inert code; don't replace the companion with app-size rounding or a fake delay.

Scope expansion to staged plasma.1 fixes should be labeled explicitly and reviewed
separately from this small window-behavior cleanup. Do not casually undo the large v2
Compose API port, scaling, persistence, launcher, or platform fixes.

## 5. Tests and rebuilding (only when needed)

Last validated: 7 Kotlin tests + 6 Python/JavaScript tests, all passing.

```sh
cd /home/alasady/Code/ab-download-manager-plasma
JAVA_HOME=/usr/lib/jvm/java-25-openjdk ./gradlew \
  -PlocalAppVersion=1.11.0+plasma.2 :desktop:app-utils:test
python3 -m unittest discover -s scripts/tests -p 'test_plasma_*.py' -v
bash -n scripts/install-plasma-local.sh
git diff --check
```

The Python behavior test runs generated JavaScript with Node; it skips if Node is absent,
so check skipped-test output instead of claiming full coverage. The Kotlin lifecycle
ordering test is a headless model, complemented by the installed-window runtime traces.

If approved cleanup changes compiled code, rebuild/install ONLY with:

```sh
JAVA_HOME=/usr/lib/jvm/java-25-openjdk ABDM_LOCAL_VERSION=1.11.0+plasma.2 \
  scripts/install-plasma-local.sh
```

This gracefully exits the running app, backs up data, installs a new release and refreshes
integration/owned companion. Relaunch with `kstart --application com.abdownloadmanager`.
Do NOT package with default Java 17 (cannot run class version 69).

Regression acceptance: animated maximized X-close and WM-close without restoring first;
normal floating close; smooth maximized restore-on-drag; fullscreen exit+drag; maximize
button/double-click; geometry survives fullscreen/close/reopen; buttons never start drags;
canceled/deferred closes must not leave surviving windows unusable.

## 6. Investigation references / tooling

- `PLASMA-LOCAL.md`: final implementation and installation log; newest follow-ups win.
- `HANDOFF-fullscreen-close.md`: newest §0c/§0b/§0a win; older sections preserve disproven
  theories. Don't repeat the whole investigation or reintroduce skip-close flags.
- KWin 6.7.5 sources: matching tag in `~/Code/kwin-upstream`, plus snapshots in
  `/tmp/probe/kwin-6.7.5/`. Its checked-out master is newer; use the matching tag.
- Actual installed effect: `/usr/share/kwin-wayland/effects/maximize/contents/code/main.js`.
- Real input: running ydotoold, socket `/run/user/1000/.ydotool_socket`.
  `YDOTOOL_SOCKET=... ydotool ...`; relative movement works with acceleration. Verify
  logical pointer location via `kdotool getmouselocation` before any click. xdotool is dead.
- `/tmp/probe/trace-animated-close-preserve.py`: set `ABDM_PID=<current pid>`; instruments
  both effects and restores the owned companion, unlike older drivers that refuse it.
- `/tmp/probe/trace-title-drag.py`: refuses an existing override; do not bypass that and
  overwrite/delete the intentional companion. Existing before/after evidence is enough
  for an audit; no need to commandeer the pointer for every cleanup candidate.
- Drag proof: `/tmp/probe/title-drag-20261015-013038/` (25 ms cancellation),
  `...-013214/` (276 ms complete). Close-with-companion proof:
  `/tmp/probe/animated-close-20261015-013509/` (~222 ms at unchanged maximized geometry).

## Suggested next-agent request

> Read HANDOFF-cleanup-audit.md first in ~/Code/ab-download-manager-plasma, branch
> local/plasma-wayland-1.11.0. Audit current changes for redundant failed-attempt
> workarounds, unused symbols, and code that no longer has an effect. Report findings
> with file/line references, provenance, and behavioral risk before changing code.
> Keep the now-confirmed close/drag behavior and intentional ABDM-only KWin companion.
> Preserve staged plasma.1 work, do not use the stale checkout, and do not commit anything.
