# Isolated desktop UI review

Developer-only tools for reviewing the **real app**, without injecting keyboard or mouse
input into the desktop, changing browser integration, or using the installed download history.
A verified native Wayland bundle and Java 25 are required. The application profile **must**
be under this checkout's ignored `build/ui-review/` directory and must not be integration-enabled.

## Build and launch

```sh
JAVA_HOME=/usr/lib/jvm/java-25-openjdk ./gradlew \
  :desktop:app:compileKotlin :shared:app:desktopTest :desktop:shared:test :desktop:app-utils:test
python3 scripts/native-wayland/build.py
python3 scripts/ui-review/run.py \
  --bundle build/native-wayland/latest \
  --profile build/ui-review/my-review-profile
```

To iterate without repackaging, add `--source`. This prepends freshly compiled app/shared UI
classes and resources to the verified bundle's classpath. Recompile and **restart only the
review instance** between passes; do not replace class files under a running review.
Omit `--source` for final bundle acceptance. Launch without `--source` against an older,
verified bundle to capture a baseline. Installed bundles themselves are never modified.

## Actions and screenshots

From another terminal, after `<profile>/ui-ready` appears:

```sh
python3 scripts/ui-review/control.py --profile build/ui-review/my-review-profile \
  --action settings --capture Settings --output build/ui-review/settings.png
python3 scripts/ui-review/control.py --profile build/ui-review/my-review-profile \
  --action close-settings --action url --capture 'New Download' \
  --output build/ui-review/new-download.png
```

Supported actions:

- `settings`, `close-settings`, `url`, `close-url`, `queues`, `close-queues`, `about`, `close-about`;
- `search text` (or `search` to clear);
- `resize WIDTH HEIGHT Window title prefix`;
- `click X Y Window title prefix`, `context X Y Window title prefix` (window-local coordinates);
- `key JAVA_KEY_CODE` (e.g. 40 Down, 38 Up, 10 Enter, 27 Escape).

Actions invoke app components or dispatch events **only to this JVM's own AWT component tree**.
The command channel is in the disposable profile. `control.py` verifies the PID and profile
before sending commands. Captures use KWin to activate a PID/title-matched window and Spectacle
to capture it; they require KDE. This is not an OS-input/IME or screen-reader acceptance test.
Context menus positioned from the OS cursor may not reflect the synthetic event coordinates.
Never invoke power actions in a UI review.

To close just this review instance gracefully:

```sh
build/native-wayland/latest/bin/ABDownloadManagerNativeWayland \
  --profile "$PWD/build/ui-review/my-review-profile" --quit
```

Wait for its PID to exit before reopening that profile. Review outputs, logs, and disposable
profiles stay under `build/ui-review/`; they are not installed or committed. The regular
native Wayland smoke suite also verifies a real loopback engine download and checksum on
Wayland and XWayland, independent profile persistence, and unchanged production integration:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 scripts/native-wayland/smoke.py
```
