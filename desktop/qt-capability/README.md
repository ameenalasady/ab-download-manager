# Qt host executable

`qt-capability-shell` is the native Wayland Qt 6 executable that hosts the Qt/KDE
frontend in [`../qt-ui/`](../qt-ui/README.md). With `--service-descriptor` it connects
to the Kotlin service in [`../service/`](../service/README.md); without one it shows
in-memory example rows (no I/O, no transfers). The name is historical: it started as a
capability probe for blur and translucency, which has since been removed.

The app identity is `org.abdownloadmanager.QtCapability[.<tag>]`. The packaged instance
(`--instance-tag abdm`) uses `com.abdownloadmanager` as its Wayland app ID so KWin, the
task manager, notifications and portals resolve the installed desktop entry.

Also built here:

- `abdm-activity-monitor`: a read-only Wayland input-idle helper (ext-idle-notify) the
  service uses for the "limit speed while I'm using the computer" policy. Built only
  when `wayland-client`, `wayland-scanner` and a new enough `wayland-protocols` exist.
- `src/desktopadapters.*`: tray icon/menu and window activation for the live service.
- `src/kwindowsystembridge.*`: optional `KWindowEffects::enableBlurBehind` bridge,
  compiled into the frontend library.

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

`KF6WindowSystem`'s CMake package is not used because its config requires
`X11Config.cmake`, which is absent here. The blur bridge is found through
`KF6WindowSystem.pc` via pkg-config; without it the host still builds and requests no blur.

## Run

Use the private-environment launcher (see
[`../../scripts/qt-capability/README.md`](../../scripts/qt-capability/README.md)):

```sh
python3 scripts/qt-capability/run.py --snapshot-desktop-style -- \
  --instance-tag preview --width 1054 --height 640
```

There is deliberately no X11 fallback. An explicit non-`wayland` `QT_QPA_PLATFORM`,
or an actual platform other than `wayland` after `QApplication` starts, exits with
status 2 before any window is created.

## CLI

```text
--service-descriptor <path>  connect to the Kotlin service (otherwise example data)
--instance-tag <tag>         app-ID suffix ([a-z0-9-], up to 48); "abdm" is the installed app
--width <px>, --height <px>  initial logical size, default 980x840, minimum 760x600
--controls-style <s>         Qt Quick Controls style, default org.kde.desktop
--resize-profile <path>      synthetic resize-to-frame timing, writes JSON and exits;
                             refused with --service-descriptor
```

The window is an ordinary decorated Wayland toplevel: KWin draws captions, shadows and
corners and handles move/resize. Swaps never block on frame callbacks, so interactive
resizes keep up with KWin (see [`../qt-ui/NATIVE-WINDOWS.md`](../qt-ui/NATIVE-WINDOWS.md)).

## Tests

`ctest` runs the frontend suites from [`../qt-ui/`](../qt-ui/TESTING.md) offscreen.

## Licensing and distribution

New source follows the repository's Apache-2.0 license. The host dynamically
uses **system** Qt/KDE/plugins; it redistributes no toolkit binaries, Layan theme,
icons or fonts. Qt base/declarative package metadata offers LGPL-3.0/GPL/commercial
alternatives; KDE components have LGPL variants; Kvantum is GPL-3.0-or-later.
Before packaging, audit exact modules/plugins and their licenses, include required
notices/license texts and corresponding source/replacement/relink information,
and review Kvantum/plugin bundling separately. Dynamic system lookup is not a
blanket exemption from distribution obligations. No GPL-only optional Qt modules
or commercial-LTS patch entitlement is assumed. KF6 bridge compilation is optional;
cross-platform support is not a goal.

API references: [Qt Quick Window](https://doc.qt.io/qt-6/qml-qtquick-window.html),
[KWindowEffects](https://api.kde.org/kwindoweffects.html),
[KF6 ext-protocol implementation](https://github.com/KDE/kwindowsystem/blob/v6.24.0/src/platforms/wayland/windoweffects.cpp),
[desktop controls style](https://github.com/KDE/qqc2-desktop-style),
[Kirigami](https://develop.kde.org/frameworks/kirigami/),
[Qt Quick accessibility](https://doc.qt.io/qt-6/accessible-qtquick.html),
[Qt LGPL](https://doc.qt.io/qt-6/lgpl.html).
