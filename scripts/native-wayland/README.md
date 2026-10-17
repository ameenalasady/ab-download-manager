# Full ABDM native Wayland experimental bundle

**Experimental Linux x64. Building does not install or change the desktop.**
A separate, explicitly approved desktop activation is available below; it preserves the
original XWayland binaries/data/integration and keeps native application state separate.

This builds the complete application (standard AppKt startup, DI, download engine, stores
and UI), not the window smoke main. It bundles the pinned Wayland-capable JBR 25 runtime
and explicitly patched Compose 1.12.1 rendering dependency. Python 3 and the normal Linux
shared libraries are required on the target; a system JVM is not required to launch.
The upstream standalone Swing rendering limitation still applies; this is experimental.

## Build without installing

Prepare the runtime/source downloads in `../wayland-probe/README.md` first. GNU patch,
Python 3.11+, and Java 25 at `/usr/lib/jvm/java-25-openjdk` are build requirements.
From this checkout on `native-wayland`:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 scripts/native-wayland/build.py
```

The builder runs the Kotlin tests and build-only release task, regenerates/verifies the
explicit experimental UI jar, extracts a **fresh copy of the checksum-verified full JBR
archive**, copies application resources/dependencies and native libraries, substitutes the
UI dependency, and adds the experimental launcher and provenance/hash manifest. It does
not use an X11-only jlinked image or mix another JDK's jlink with JBR modules. No matching
SDK download is needed because this bundle contains the complete pinned runtime.
Original packaged jars remain unchanged. No normal launcher or installer is invoked.

Outputs under ignored `build/native-wayland/`:

- `releases/ABDownloadManagerNativeWayland-<timestamp>/`: self-contained versioned bundle;
- `latest`: build-local symlink to that bundle, NOT the installed app's `current`;
- `ABDownloadManager-native-wayland-experimental-linux-x64.tar.gz` plus `.sha256`.

The archive retains runtime/app licensing. The bundled `bundle-manifest.json` records the
JBR archive digest, renderer input/output/patch digests, Git HEAD/dirty status and hashes
of every bundle file. The launcher refuses changed, missing or extra files, symlink escapes
and redundant launcher manifest classpaths. Rebuild rather than editing a bundle in place.

## External file/browser handoff (native.3; explicitly approved and activated)

ABDM's HOME/XDG isolation must not select a different media player or browser. The launcher
captures a small original desktop environment allowlist before isolation and preserves it
across internal host/CLI/GUI cold starts. An explicitly integration-enabled profile validates
that original HOME against its integration policy, then applies the snapshot only to external
command helpers. Unset XDG paths are removed so the user's normal desktop overrides/defaults
are found. File/folder openers use this scoped environment; Linux URL opening avoids AWT's
unscoped native libgio launch in this case. No desktop associations are rewritten, and ABDM's
own stores/JVM/internal detached launches retain profile isolation. The private context is
removed from external children. Nonactivated profiles and ordinary app behavior are unchanged.

A non-window, recorder-only test checks the actual bootstrap and public openers with a fake
HOME, custom XDG paths, and conflicting system/user entries under the same desktop ID:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 scripts/native-wayland/external_open_smoke.py
```

It compiles a small Java harness using Java 25 and the newly built bundle, runs the real
`xdg-open` generic backend with fixture MIME defaults, and never launches real VLC/media or
changes real desktop settings. Output: `build/native-wayland/external-open-results.json`.
The user approved installing `1.11.0+native.3` and gracefully restarting after idle checks.
Release `ABDownloadManagerNativeWayland-20261016-000710` is now installed and left running
with existing native settings/history retained during the upgrade. Later runtime changes
are not overwritten. VLC/MIME defaults and fallback data/binaries/KWin remain untouched.
The latest guarded upgrade rollback is
`~/.local/state/abdm-backups/native-integration-20261016-001153-bf4f31/restore-integrations.sh`
(native.3 -> fully integrated native.2). Undo this upgrade before using the older original
integration rollback. See `NATIVE-WAYLAND.md` for installed verification and backup details.
No real video playback was automated. Do not patch the immutable bundle or restart the
real-use session again without approval.

## Optional user-approved desktop activation

The user approved this step for real-life testing after reviewing the full bundle:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 scripts/native-wayland/install.py --activate-desktop
# Installed native launcher (not dependent on this checkout/build directory):
~/.local/bin/ABDownloadManagerNativeWayland
# Quit only its persistent native profile:
~/.local/bin/ABDownloadManagerNativeWayland --quit
```

The installer verifies and copies the bundle under
`~/.local/opt/ab-download-manager-native-wayland/releases/`, switches that separate `current`,
adds the native wrapper/icon, and replaces **only the existing same-ID user desktop entry**
with **AB Download Manager (Native Wayland)**. It validates the entry, uses the native
Wayland app ID for StartupWMClass, refreshes desktop/KDE caches, and prints a backup/rollback
path. Unowned installs and unrelated/modified native wrappers are rejected.

Installed native data stays in `~/.local/share/ab-download-manager-native-wayland/profile`,
with initial downloads at `~/Downloads/ABDM-Native-Wayland`. Existing native preferences/
history survive reinstalls. Normal production settings/history are never copied. Do not
run the internal smoke flags against this real-use profile.

The backup's `restore-desktop.sh` restores the exact previous desktop entry and cache,
refusing intervening edits. It does not stop running downloads or delete native data/
releases. Neither activation nor rollback changes the regular XWayland wrappers/current
link, browser manifests, autostart, system package or KWin companion. Existing XWayland
processes remain running; browser-extension traffic continues to use that old integration.
Native browser capture/tray/autostart/update/keep-awake/power remain deliberately disabled.

## Full native integration (separately and explicitly approved)

Desktop-only activation above intentionally leaves old integrations alone. The user later
requested full native use. This second opt-in performs that takeover:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 scripts/native-wayland/build.py
PYTHONDONTWRITEBYTECODE=1 python3 scripts/native-wayland/integration_smoke.py
PYTHONDONTWRITEBYTECODE=1 python3 scripts/native-wayland/desktop_integration.py --activate
kstart --application com.abdownloadmanager
```

Integration-capable bundles (native.2 and later) support quiet `--native-host` and `--cli`
modes and `--background`. The activation helper refuses active transfers/unowned helpers,
exits the old idle app and prior native GUI gracefully, installs the verified candidate,
and backs up the old integration selection. It creates a canonical profile-local explicit
policy, then switches **all ordinary ABDownloadManager/CLI/native-host wrappers**, the seven
Firefox/Chromium host manifests, and real-user autostart to native. The actual native GUI
owns default HTTP port **15151** and independent profile IPC. Browser-native requests can
cold-start the native GUI through dedicated native CLI/main paths, never the fallback.
Native host stdout is **only length-prefixed JSON**, not launch banners. Existing extension
IDs/origins and the browser protocol name `com.abdownloadmanager` are retained.

Existing native settings/history are kept; the first activation enables API/port/tray/
autostart, while later activations retain user choices. Normal old data is not imported.
Tray, keep-awake and user-selected power actions are available normally. No destructive
power action is invoked in validation. **Upstream self-updating remains disabled** because
its packages do not preserve the required runtime/renderer. Use an audited rebuild instead.

The printed `restore-integrations.sh` checks for intervening edits/active transfers, exits
native gracefully and restores the prior browser/CLI/autostart/current/desktop selection,
retaining all native downloads/history. For this workstation that prior state was a native
desktop with old XWayland integration; use the older desktop-only restore **after** the full
integration restore to return the desktop to XWayland as well. Desktop-only restore on its
own no longer undoes a full takeover. Do not run the fallback alongside native without
review: its original startup can reinstall old browser manifests or take port 15151.

`integration_smoke.py` uses a **fake home** and conspicuous disposable window titles. It
checks framed host ping/add, native GUI cold start, actual PID-owned tray registration,
HTTP and native-host engine downloads against a local fixture with SHA-256, category
subfolders, background relaunch/persistence, single-instance, graceful exit and full rollback.
It never uses the real native test profile. Protocol-level tests do not replace real
browser-extension UI acceptance; reconnect/reload an already-open extension if needed.

## Launch the full app separately

```sh
build/native-wayland/latest/bin/ABDownloadManagerNativeWayland
# Explicit XWayland diagnostic control, NOT a change to the installed fallback:
build/native-wayland/latest/bin/ABDownloadManagerNativeWayland --backend x11
# Gracefully quit only this bundle's default experimental native profile:
build/native-wayland/latest/bin/ABDownloadManagerNativeWayland --quit
# Verify the bundle without opening windows or creating a profile:
build/native-wayland/latest/bin/ABDownloadManagerNativeWayland --verify-only
```

Native mode requires a live WAYLAND_DISPLAY, explicitly selects WLToolkit and **unsets
DISPLAY**, so it cannot silently fall back to XWayland. Inherited JVM option injection and
library overrides are removed. The Java entry class is
`com.abdownloadmanager.desktop.experimental.NativeWaylandAppKt`; it delegates to the real
AppKt, with a unique desktop identity and titles ending in `Native Wayland EXPERIMENT`.

Profiles default to `profiles/wayland` or `profiles/x11` beside the versioned bundle,
separate from installed ABDM stores. To select another **empty or already owned** profile:

```sh
build/native-wayland/latest/bin/ABDownloadManagerNativeWayland --profile /absolute/path/to/empty-profile
```

The launcher creates an ownership marker and separate HOME, user.home, XDG config/data/cache,
temp, default Downloads and `app-data` directories. The Kotlin bootstrap validates these
before AppInfo/data stores/single-instance discovery initialize, and AppInfo explicitly
routes application data there rather than consulting normal/portable stores. Symlinked
profiles/app data and nonempty unowned directories are rejected. No production settings
or downloads are copied. Do not point a profile at an installed data directory.

Single-instance locking/IPC uses the profile's own config directory and an independent
OS-assigned loopback port. In default disposable/nonactivated profiles, **browser integration
server boot, manifest install/removal,
desktop-entry creation, autostart installation/removal, system tray, self-update checking/
applying/cleanup, keep-awake and power actions are disabled at application boundaries**, not
just via initial preferences. Preferences alone cannot turn these on; the separately approved
integration policy above enables them, except updating. The normal XToolkit app with no
experimental profile retains its existing behavior.

This is application-level isolation, **not a security/OS filesystem sandbox**. Manual
file dialogs/download destinations and external-app commands can still access paths allowed
to your user. Ordinary downloading/network access is intentional. Use only disposable test
downloads; do not import your normal database. Clipboard/file dialogs, browser integration,
tray, notifications, IME, full-screen handling, fractional/mixed-DPI correctness and rendering
performance are not implied by a successful startup.

Closing/quitting this experimental app uses only its profile's IPC, not the installed
instance's. `--quit` also accepts `--profile`/`--backend` and routes the internal CLI exit
command through that same validated profile. CLI/native-host modes use this profile; normal command aliases and real browser registration
are added only by the explicit full-integration activation above. Settings and download history in this profile persist between runs. Build-only/separate
launch commands do not modify the installed desktop. Only the explicitly approved activation
above switches the KDE entry and adds a native wrapper; original wrappers/stores and the
intentional KWin companion are not modified. To remove it, delete only this build output and its owned test
profiles after closing the experimental process; do not run the normal uninstaller.

## Bounded full-app checks

```sh
PYTHONDONTWRITEBYTECODE=1 python3 scripts/native-wayland/smoke.py
```

The supervisor uses fresh profiles, native protocol/library evidence and independent IPC
checks, times out only its own subprocesses, and runs native plus explicit XWayland controls.
The `--smoke-test` launcher flag enables in-process UI checks: real home/settings/URL-entry
windows, maximize/restore, settings side effects, and the graceful engine-stop/settings-flush
exit path. The supervisor also serves a disposable 1 MiB payload on its own OS-assigned
127.0.0.1 HTTP port. The **actual download engine** downloads it into the isolated Downloads
folder; both the application and supervisor verify SHA-256. No public download URL is used.
Each backend is then reopened on the same profile to verify persisted history, second-launch
negotiation and profile-scoped IPC quit. Production file/integration/KWin hashes, installed
release and installed PID are compared before/after. Unexpected GUI/shutdown exceptions fail
the test even with exit 0. Experimental exit uses Compose's EDT window disposal before JVM
shutdown, avoiding the direct-exit WLToolkit shutdown-hook failure found in the initial run.

To test archive relocation, extract it into an empty build-local directory and use:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 scripts/native-wayland/smoke.py --bundle /absolute/path/to/extracted/bundle
```

These are programmatic actions, not real keyboard/pointer/IME acceptance. Logs and session-
status records remain under ignored build output. See `NATIVE-WAYLAND.md` for measured
results and outstanding limitations before proposing installation.
