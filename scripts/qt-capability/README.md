# Safe capability-shell launcher

`run.py` launches the isolated Qt6 Wayland capability shell or its separate opaque
backdrop process. It is a test utility, not an app launcher, installer, profile
manager, or production integration hook.

Guarantees:

- Refuses any explicit `QT_QPA_PLATFORM` other than `wayland`.
- Requires `WAYLAND_DISPLAY` and an existing, user-owned Unix socket.
- Forces `QT_QPA_PLATFORM=wayland` and removes `DISPLAY` from the child.
- Creates a fresh `0700` temp tree under `build/qt-capability/run/`.
- Uses private writable `HOME`, `XDG_CONFIG_HOME`, `XDG_CACHE_HOME`,
  `XDG_DATA_HOME`, `XDG_STATE_HOME`, `XDG_RUNTIME_DIR`, and `TMPDIR`.
- Symlinks only the existing Wayland socket into the private runtime dir.
- Preserves read-only lookup paths (`XDG_DATA_DIRS`, `XDG_CONFIG_DIRS`,
  `FONTCONFIG_*`, `QML*_IMPORT_PATH`, `QT_PLUGIN_PATH`, desktop/session vars) so
  system styles, icons, fonts, and QML modules remain discoverable.
- `--snapshot-desktop-style` copies only kdeglobals, the Kvantum selector and
  selected `.kvconfig`/`.svg` theme data into disposable config. No source profile
  or writable link back to real config is used. This opt-in resolved Layan via
  Kvantum in the live tests; without it user-specific styling may be absent.
- Never sets a Kvantum or widget-style override; the shell selects its Quick
  style explicitly and reports the resolved widget/Quick styles.
- Routes the probe's default-category info receipts to stderr, not just the journal.
- Bounded backdrop patterns only: `stripes`, `black`, `white`, `magenta`.
- No production profile argument, engine or production-port listener. Default
  capability/fixture views perform no transfers. The explicit downloads
  `--service-descriptor` option connects to the private isolated Kotlin service;
  see [`../../desktop/service/README.md`](../../desktop/service/README.md).
  The wrapper never starts the service or opens its databases.

## Usage

Build first:

```sh
./scripts/qt-capability/build.sh
```

Then, from the repository root:

```sh
# Normal shell; all arguments after -- go to the shell
python3 scripts/qt-capability/run.py -- --quit-after-ms 4000

# Opaque backdrop (default stripes) in a separate process, then the shell
python3 scripts/qt-capability/run.py --snapshot-desktop-style --with-backdrop

# Backdrop only, solid pattern for repeatable A/B
python3 scripts/qt-capability/run.py --backdrop-only --backdrop-pattern black
python3 scripts/qt-capability/run.py --with-backdrop --backdrop-pattern white

# Programmatic checks and a client-side alpha frame
python3 scripts/qt-capability/run.py -- --self-test --frame-out build/qt-capability/selftest.png

# First interface migration slice: in-memory example rows, no engine or transfers
python3 scripts/qt-capability/run.py --snapshot-desktop-style -- --view downloads --instance-tag migration-preview --width 1100 --height 840

# Inspect the isolated plan without launching anything
python3 scripts/qt-capability/run.py --dry-run --keep-temp
```

`--dry-run` prints JSON containing the commands, private paths, and relevant
child environment. The temp tree is removed unless `--keep-temp` is given.
`--binary` can point at another build for dry-run tests.

## Live rendering and geometry (explicit opt-in)

```sh
python3 scripts/qt-capability/live_check.py --run --output build/qt-capability/new-rendered-proof
python3 scripts/qt-capability/geometry_check.py --run --output build/qt-capability/new-geometry-proof
```

Output directories must be new. These use existing desktop tools and Python
Pillow/numpy; they do not install anything. Only spawned synthetic windows are
acted on, after PID and app-ID checks. The geometry script uses the inventoried
primary/secondary coordinates (1.25/1.0 scales); it does not change output settings.
The capture helper validates shell PID/app ID before activation, polls every
0.1 s and retries every 0.5 s while inactive within a 5 s acquisition deadline
(individual desktop commands have separate 20 s timeouts). It requires 0.8 s of
continuous verified focus for compositor settling; lost focus resets that interval.
Immediately before and after Spectacle it requires the active window to be the
owned shell. It refuses to invoke Spectacle without verified focus, and retains
no crop if a post-capture focus/identity/geometry guard fails. Per-capture
`*-compositor.png.json` records status and activation attempts even on failure,
without recording unrelated active-window identities.

Accepted captures are crops over the owned public backdrop. Private full-desktop
intermediates are removed from a private RAM directory in a finally block.
Cleanup errors preserve the original error, flag possible full-frame retention
and cannot return capture success. Hard termination can still leave a private
RAM frame; deletion is not guaranteed against it. Backdrop identity/geometry is
checked before and after the screenshot. Declared solid/geometry-derived stripe
padding colors are validated before gate aggregation, with explicit contamination
failure rather than misleading framework booleans.
Live opacity inspection uses a transient read-only KWin query, not a settings edit.
Focus/geometry guards are not an exhaustive detector of foreign notifications,
always-on-top overlays or stale compositor frames. A first capture before the
continuous-focus settling fix was contaminated despite valid focus/geometry;
its pixel metrics failed. Inspect crops locally; **do not upload raw captures**.
Preserve failed logs/evidence and use a new output directory rather than retrying
into an existing one.

Programmatic state changes and QAccessible names do not establish real
pointer/IME/screen-reader acceptance.

## Resize sweep

`resize-sweep/` measures interactive-resize throughput the way an edge drag exercises
it. `sweep.sh` starts a nested KWin (a window on the current Wayland session) on a
private session bus with private copies of `kwinrc`, `kdeglobals`, `kcminputrc` and
Kvantum config, launches the client, and loads `sweep.qml`, a KWin script that changes
the window width every 8 ms. KWin applies a size only after the client acknowledged
and committed it, so the report counts real round trips:

```sh
scripts/qt-capability/resize-sweep/sweep.sh abdm QtCapability \
  "$PWD/build/qt-capability/qt-capability-shell" --view downloads
scripts/qt-capability/resize-sweep/sweep.sh dolphin dolphin dolphin --new-window
# -> ABDM-SWEEP {"requested":240,"applied":...,"gapP50":...,"gapP95":...,"gapMax":...}
```

`capture.sh <shell> <out.png>` places the synthetic downloads view over the shell's
striped backdrop in a nested KWin and captures the screen through ScreenShot2
(`KWIN_SCREENSHOT_NO_PERMISSION_CHECKS` is set for the nested instance only), to check
that blur covers the window and does not spill outside it. Neither script sends input
or writes the user's configuration.

## Tests

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s scripts/qt-capability/tests -v
```

The tests create a real Unix-domain socket in a temporary runtime dir, exercise
dry-run planning and refusals, verify the socket symlink and private environment,
and scan the new sources for forbidden production-engine/port hooks. Additional
tests exercise screenshot guards/cleanup with fake images and check explicit AA
contrast tokens. They never launch a live GUI (CTest separately loads QML offscreen).
This is application-level isolation, not an OS filesystem/security sandbox; the
existing session bus and read-only plugin/data lookup paths remain available.
