# Qt6 Wayland capability shell

Native Wayland Qt 6 host executable. With `--view downloads` it hosts the Qt/KDE
frontend in [`../qt-ui/`](../qt-ui/README.md), using in-memory examples unless
`--service-descriptor` points it at the service in [`../service/`](../service/README.md).
Its default view is a capability probe: a translucent Qt Quick window that keeps window
opacity at 100%, carries a deliberate alpha background and requests KWin blur behind
it, with diagnostics for the platform, style and material. It starts no service and
never opens the production integration port.

The shell identity is `org.abdownloadmanager.QtCapability`. The separate opaque
backdrop process uses `org.abdownloadmanager.QtCapability.Backdrop`. The real experimental Kotlin Wayland entrypoint is never loaded, linked, or
referenced by these sources.

## Build

From the repository root:

```sh
./scripts/qt-capability/build.sh
```

Equivalent manual commands:

```sh
cmake -S desktop/qt-capability -B build/qt-capability -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/qt-capability -j 4
ctest --test-dir build/qt-capability --output-on-failure
```

All build output, resources, and runtime temp state stay under the ignored
`build/qt-capability/` tree. No files are installed and no desktop entries,
profiles, configs, or registrations are written.

`KF6WindowSystem`'s CMake package is not used because its config requires
`X11Config.cmake`, which is absent here. The optional `KWindowEffects::enableBlurBehind`
bridge is found through `KF6WindowSystem.pc` via pkg-config. If pkg-config or the
library is unavailable, the shell still builds and reports the bridge as absent.

## Run

Use the safe launcher, not a raw command:

```sh
python3 scripts/qt-capability/run.py -- --self-test --frame-out build/qt-capability/selftest.png
python3 scripts/qt-capability/run.py -- --quit-after-ms 4000
python3 scripts/qt-capability/run.py --snapshot-desktop-style --with-backdrop
```

The safe launcher removes `DISPLAY`, forces `QT_QPA_PLATFORM=wayland`, refuses
any other platform, creates private writable `HOME`/XDG directories, and links
only the existing Wayland socket into a private `XDG_RUNTIME_DIR`. See
[`../../scripts/qt-capability/README.md`](../../scripts/qt-capability/README.md).

Direct invocation is possible only when `QT_QPA_PLATFORM` is unset or exactly
`wayland` and the current session already has a usable Wayland socket:

```sh
QT_QPA_PLATFORM=wayland ./build/qt-capability/qt-capability-shell --self-test
```

There is deliberately no X11 fallback. An explicit non-`wayland`
`QT_QPA_PLATFORM`, or an actual platform other than `wayland` after
`QApplication` starts, exits with status 2 before any window is created.

## Shell behavior

- `QApplication` plus `QQuickView` (Qt Widgets linked for `org.kde.desktop`).
- Explicit `QQuickStyle` selection, default `org.kde.desktop`; no assumption that
  QML inherits Kvantum. Widget style and Quick style are reported separately.
- Frameless, transparent `QQuickView` with `windowOpacity == 1.0` and an alpha
  buffer. The translucent background is a deliberate `#CC1B1B24` color.
- 16 logical-pixel transparent shadow margin; 18 logical-pixel rounded content.
  A hollow client shadow occupies this margin. The rounded silhouette is inset
  from the window edge; Qt Quick MultiEffect masks the complete child subtree
  (Rectangle.clip alone is rectangular). The maximized state switches to zero margin and zero corner
  radius, updating both the input mask and blur region.
- `QWindow::setMask` receives the same rounded logical region as the
  `KWindowEffects::enableBlurBehind` request. This is a request only: neither
  compositor application of the input mask nor rendered blur is claimed.
- System move and resize through `startSystemMove`/`startSystemResize`; title-bar
  double-click toggles maximize/restore.
- Opaque mock rows, labels, text input, controls, menu, dialog, and a magenta
  opaque swatch. A separate empty item named `translucentMeasurementRegion`
  contains no child UI and is used for alpha sampling.
- Blur toggle and reduced-transparency/opaque mode. Opaque mode forces the blur
  request off.
- Optional Kirigami dialog probe loads only when `org.kde.kirigami` is available
  on the QML import path.
- The separate backdrop process accepts a bounded `stripes|black|white|magenta`
  pattern. Solid patterns hide the stripe field and center card so a repeatable
  solid-color backdrop remains for blur A/B testing.
- Diagnostics report platform, app id, Quick/widget style, device pixel ratio,
  window opacity, alpha buffer size, logical/device geometry, content rectangle,
  mask region, blur region, bridge status, the API's blur-availability report,
  whether a request was issued, reduced-transparency state, Kirigami
  availability, and the logical geometry of the UI-free measurement region,
  opaque swatch, and first synthetic row. The diagnostics explicitly say
  rendering is unverified.

## CLI

```text
--backdrop             opaque public backdrop, no shell UI, no blur
--backdrop-pattern <p> backdrop pattern: stripes, black, white, magenta (default stripes)
--self-test            geometry/alpha checks, save frame, exit
--quit-after-ms <ms>   quit timer
--frame-out <path>     save a client-side framebuffer PNG
--width <px>           logical width, default 980
--height <px>          logical height, default 840
--controls-style <s>   Qt Quick Controls style, default org.kde.desktop
--instance-tag <tag>   bounded disposable app-ID suffix, avoids remembered geometry
--view <name>          capability (default) or downloads (migration interface preview)
--probe-view <name>    none (default), menu, dialog, kirigami; capability view only
--self-test-delay-ms N delay 0..10000 ms for supervised geometry checks
--no-blur              start with the blur request disabled
--opaque              start in opaque/reduced-transparency mode
```

`--self-test` checks the actual platform, window opacity, alpha buffer, region
consistency, rounded-corner exclusion, a saved framebuffer with an alpha
channel, transparent exterior pixels, translucent measurement alpha (or opaque
in `--opaque`), and the opaque swatch alpha. A saved frame is client-side alpha
evidence, **not** proof of compositor blur.

## Tests

`ctest` runs offscreen-only tests:

- `tst_regiongeometry`: rounded-region bounds, corner exclusion, margin
  exclusion, maximized squared bounds, input/blur identity, and tiny/empty sizes.
- `tst_qmlload`: loads `Main.qml` offscreen with a mock controller, verifies the
  backdrop solid-color mappings, and compiles the optional Kirigami dialog QML
  when the module is installed.

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s scripts/qt-capability/tests -v
```

The launcher tests use a local Unix socket, never launch the GUI, and verify the
private environment, socket symlink, refusal of X11/offscreen overrides, and the
absence of production-engine/port hooks.

## Live validation and evidence limits

```sh
# Explicitly opens/moves only disposable proof windows; output must be new.
python3 scripts/qt-capability/live_check.py --run --output build/qt-capability/my-rendered-proof
python3 scripts/qt-capability/geometry_check.py --run --output build/qt-capability/my-geometry-proof
```

These require kdotool, Spectacle, Pillow and numpy. The screenshot helper refuses
foreign PIDs/classes, unstable geometry/focus, uncovered backdrop crops and
inconsistent coordinate mapping, retries owned-shell activation and removes private
full-desktop intermediates. Keep raw evidence local. These checks are not an OS
sandbox, an unattended interactive-input test, or a safe generic way to manipulate
arbitrary windows; real pointer, IME, screen-reader and snap/tile behavior still need
manual checking.

For interactive-resize throughput and blur-region checks in a nested KWin, see
[`../../scripts/qt-capability/README.md`](../../scripts/qt-capability/README.md#resize-sweep).

## Licensing and distribution

New source follows the repository's Apache-2.0 license. This proof dynamically
uses **system** Qt/KDE/plugins; it redistributes no toolkit binaries, Layan theme,
icons or fonts. Qt base/declarative package metadata offers LGPL-3.0/GPL/commercial
alternatives; KDE components have LGPL variants; Kvantum is GPL-3.0-or-later.
Before packaging, audit exact modules/plugins and their licenses, include required
notices/license texts and corresponding source/replacement/relink information,
and review Kvantum/plugin bundling separately. Dynamic system lookup is not a
blanket exemption from distribution obligations. No GPL-only optional Qt modules
or commercial-LTS patch entitlement is assumed. KF6 bridge compilation is optional;
cross-platform application support is not established by this proof.

API references: [Qt Quick Window](https://doc.qt.io/qt-6/qml-qtquick-window.html),
[KWindowEffects](https://api.kde.org/kwindoweffects.html),
[KF6 ext-protocol implementation](https://github.com/KDE/kwindowsystem/blob/v6.24.0/src/platforms/wayland/windoweffects.cpp),
[desktop controls style](https://github.com/KDE/qqc2-desktop-style),
[Kirigami](https://develop.kde.org/frameworks/kirigami/),
[Qt Quick accessibility](https://doc.qt.io/qt-6/accessible-qtquick.html),
[Qt LGPL](https://doc.qt.io/qt-6/lgpl.html).
