# Testing the Qt frontend and service

All commands run from the repository root and use only disposable state: private
temporary profiles, fake or owned loopback services, offscreen rendering. They never
touch an installed release, a real profile, a running application or desktop settings.

## Commands

```sh
# Qt: geometry, host QML load, preview model, downloads view, service adapter,
# desktop opener and native window style (7 CTest suites).
cmake -S desktop/qt-capability -B build/fast-tests -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/fast-tests -j$(nproc)
ctest --test-dir build/fast-tests -j$(nproc) --output-on-failure

# Kotlin service.
./gradlew :desktop:service:test --rerun --configuration-cache -PlocalAppVersion=1.0.0-test
# Including the notification test, which needs its own bus:
dbus-run-session -- env ABDM_TEST_PRIVATE_BUS=1 ./gradlew \
  :desktop:service:test --rerun --configuration-cache -PlocalAppVersion=1.0.0-test

# Python tooling.
python3 -m unittest discover -s scripts/qt-service -p 'test_*.py'
python3 -m unittest discover -s scripts/qt-capability/tests -p 'test_*.py'

# QML lint (use Qt 6's qmllint, not an older /usr/bin/qmllint).
/usr/lib/qt6/bin/qmllint -I build/fast-tests desktop/qt-ui/qml/*.qml
```

The only expected lint warning is that `Tokens.qml` uses `pragma Singleton` while the
qmldir declaration is supplied by the build.

`--rerun` forces just the test task, not compilation. `-PlocalAppVersion` avoids the
Git-version plugin's configuration-time Git subprocesses, which otherwise break the
configuration cache; it labels a **test build**, so do not use it for releases. Add
`--no-daemon` for CI-style runs.

Real-engine end-to-end checks (owned loopback fixture server, real engine, restart,
checksums, Qt adapter and QML):

```sh
PYTHONDONTWRITEBYTECODE=1 python3 scripts/qt-service/smoke.py
PYTHONDONTWRITEBYTECODE=1 python3 scripts/qt-service/smoke.py --native-qml   # native Wayland
PYTHONDONTWRITEBYTECODE=1 python3 scripts/qt-service/runtime_smoke.py --native-ui
```

## Running one Qt case

`ctest` sets `QT_QPA_PLATFORM=offscreen` and `QT_QUICK_BACKEND=software`. A direct run
does not, and would open windows on the live display, so set them yourself:

```sh
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  build/fast-tests/qt-ui/tst_servicedownloads duplicatesAreChosenInlineBeforeAdding
```

`QT_ADAPTER_CAPTURE_DIR=<dir>` makes cases that support it save client captures.
Add `QT_QPA_PLATFORMTHEME=kde` for the desktop palette.

### Gallery

Offscreen client captures of every main surface, for visual review:

```sh
mkdir -p /tmp/abdm-gallery
QT_PARITY_CAPTURE_DIR=/tmp/abdm-gallery QT_QPA_PLATFORM=offscreen \
  QT_QPA_PLATFORMTHEME=kde QT_QUICK_BACKEND=software \
  build/fast-tests/qt-ui/tst_servicedownloads parityGallery
```

Use an empty directory each time; file indices change between versions. Client
captures are not compositor screenshots; for native window behaviour use
`smoke.py --native-qml` or the nested-KWin tools in
[`scripts/qt-capability`](../../scripts/qt-capability/README.md).

## Isolation and concurrency

- `tst_servicedownloads` runs every slot in a fresh child process (120 s deadline, no
  retries, buffered output, per-case `CASE` timing and a final list of all failures).
  `ABDM_TEST_JOBS=N` sets the child concurrency (default min(8, cores); `1` for
  sequential diagnosis). Passing a slot name keeps the normal QtTest behaviour.
- Per-case processes are required: repeated QML-engine teardown in one process can
  corrupt the heap in the Qt/KDE stack. This isolates the tests; it does not fix that
  defect.
- GUI suites use offscreen/software rendering, private temporary sockets and
  PID-qualified identities, so `ctest -j` needs no locks.
- Real-engine `QT_SERVICE_TEST_DESCRIPTOR`, capture directories and `QT_*_HOLD_MS`
  force a single child, because they share a service, display or output paths.
- `-PabdmTestForks=N` sets the service's JVM fork count (default min(8, cores)). Test
  classes own unique temporary profiles and ephemeral loopback ports. Browser tests
  that need a concrete port reserve one from 20000–29999 under a lock file in
  `$TMPDIR/abdm-test-ports/`.
- `ABDM_TEST_PRIVATE_BUS=1` is only safe inside a fresh `dbus-run-session`; the
  notification test claims a fixed bus name.

## Writing tests

- Wait for a rendered frame (`waitForRenderedFrame`) instead of sleeping before image
  or geometry checks. Keep deliberate observation windows (late-geometry, no-replay,
  debounce): their duration is part of the scenario.
- Close native popups/dialogs and drain `QEvent::DeferredDelete` before the view is
  destroyed, and let the fake service finish its fragmented replies before its
  sockets go away.
- Do not read a dialog's `header`/`footer` unless it is open: that forces the desktop
  style's deferred items into existence.
- The browser review window destroys itself after Accept; hold references through
  `QPointer`.
- Use real pointer events for interaction that depends on grabs (combo popups, radio
  buttons) rather than setting properties.
- Never point tests at a real profile or the production integration port (15151).
