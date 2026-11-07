# Qt frontend scripts

- `build.sh`: configure, build and test the Qt host and frontend in `build/qt-capability/`.
- `run.py`: launch the frontend with private writable state (below).
- `resize-sweep/`: interactive-resize throughput in a nested KWin (below).

## Private-environment launcher

`run.py` launches the Qt frontend with a private HOME/XDG tree. The packaged app ships
it as `lib/ui_environment.py`, and `scripts/qt-service/runtime.py` imports it to build
the frontend's environment, so it is production code, not only a dev tool.

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
- Routes the frontend's info logging to stderr, not just the journal.
- No production profile argument. Without `--service-descriptor` the frontend shows
  example data and performs no transfers; with it, it connects to an already running
  service (see [`../../desktop/service/README.md`](../../desktop/service/README.md)).
  The wrapper never starts the service or opens its databases.

```sh
./scripts/qt-capability/build.sh

# Example data; all arguments after -- go to the frontend
python3 scripts/qt-capability/run.py --snapshot-desktop-style -- --instance-tag preview --width 1100 --height 840

# Inspect the isolated plan without launching anything
python3 scripts/qt-capability/run.py --dry-run --keep-temp
```

`--dry-run` prints JSON containing the command, private paths, and relevant child
environment. The temp tree is removed unless `--keep-temp` is given. `--binary` can
point at another build.

## Resize sweep

`resize-sweep/` measures interactive-resize throughput the way an edge drag exercises
it. `sweep.sh` starts a nested KWin (a window on the current Wayland session) on a
private session bus with private copies of `kwinrc`, `kdeglobals`, `kcminputrc` and
Kvantum config, launches the client, and loads `sweep.qml`, a KWin script that changes
the window width every 8 ms. KWin applies a size only after the client acknowledged
and committed it, so the report counts real round trips:

```sh
scripts/qt-capability/resize-sweep/sweep.sh abdm QtCapability \
  "$PWD/build/qt-capability/qt-capability-shell"
scripts/qt-capability/resize-sweep/sweep.sh dolphin dolphin dolphin --new-window
# -> ABDM-SWEEP {"requested":240,"applied":...,"gapP50":...,"gapP95":...,"gapMax":...}
```

The script sends no input and writes none of the user's configuration.

## Tests

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s scripts/qt-capability/tests -v
```

The tests create a real Unix-domain socket in a temporary runtime dir and exercise
dry-run planning, refusals, the socket symlink, the private environment and the style
snapshot. They never launch a GUI. This is application-level isolation, not an OS
filesystem/security sandbox; the session bus and read-only lookup paths stay available.
