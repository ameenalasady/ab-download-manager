# Isolated native Wayland smoke tests

**Experimental only. Do not use this as an installer or modify the normal launcher.**

These are the retained sources from the feasibility test in `WAYLAND-FEASIBILITY.md`.
They use the exact packaged ABDM libraries and its real `CustomWindow` wrapper, but never
start ABDM's entrypoint, download engine, data stores, tray, or integration server.

All runtime downloads, generated overlay source/classes, logs, and test home/config/data
stay in ignored `build/wayland-probe/`. Nothing under that directory belongs in Git.
The original local scratch scripts remain there as historical artifacts; use the scripts
in **this directory** going forward, since they survive a fresh clone.

## Requirements

- Linux with an accessible Wayland/KWin session; an XWayland `DISPLAY` for the control case.
- Java 25 at `/usr/lib/jvm/java-25-openjdk` (adjust `compile.py` if using another location).
- Python 3, curl, tar; Kotlin/Compose compiler 2.4.20 cached by the normal Gradle build.
- Packaged app artifacts with Compose 1.12.1 and Skiko 0.150.1. These probes deliberately
  pin the measured baseline; don't interpret them as a general framework compatibility test.

From the repository root, build artifacts **without installing/restarting the normal app**
if they are not already present:

```sh
JAVA_HOME=/usr/lib/jvm/java-25-openjdk ./gradlew \
  -PlocalAppVersion=1.11.0+plasma.2 \
  :desktop:app-utils:test :desktop:app:createReleaseDistributable
```

## Prepare the pinned scratch runtime and source archive

The original workstation already has these downloads. On a fresh clone, run from the
repository root. The JBR archive checksum was verified against its official release;
source checksums also prevent silently patching a different framework revision.

```sh
mkdir -p build/wayland-probe/runtime
curl -fL --connect-timeout 15 --max-time 180 \
  https://cache-redirector.jetbrains.com/intellij-jbr/jbr-25.0.4.1-linux-x64-b635.72.tar.gz \
  -o build/wayland-probe/jbr-25.0.4.1-linux-x64-b635.72.tar.gz
printf '%s  %s\n' \
  b7cff4d94f5e8240024834d966451c15275b9d5ed01b8a806b0d4ef84248abb53b0b58beb40a8518ab1d2bba8d36878e04424e2251e3b0be87098fed219cc47d \
  build/wayland-probe/jbr-25.0.4.1-linux-x64-b635.72.tar.gz | sha512sum --check
# Extract only after the checksum passes.
tar -xzf build/wayland-probe/jbr-25.0.4.1-linux-x64-b635.72.tar.gz \
  -C build/wayland-probe/runtime --strip-components=1
curl -fL --connect-timeout 15 --max-time 60 \
  https://repo.maven.apache.org/maven2/org/jetbrains/compose/ui/ui-desktop/1.12.1/ui-desktop-1.12.1-sources.jar \
  -o build/wayland-probe/ui-sources.jar
printf '%s  %s\n' \
  ae1ccb98d6d7335c7e2d0ec5024e6cda800d1737bab3477f81c45914e27b786e \
  build/wayland-probe/ui-sources.jar | sha256sum --check
```

Use a shell with error checking (for example `set -e`) or stop on any download/checksum
failure. These instructions do not change system Java, wrappers, desktop entries, or KWin.

## Compile and run

```sh
PYTHONDONTWRITEBYTECODE=1 python3 scripts/wayland-probe/compile.py --overlay
PYTHONDONTWRITEBYTECODE=1 python3 scripts/wayland-probe/compile.py
PYTHONDONTWRITEBYTECODE=1 python3 scripts/wayland-probe/run.py
# Or just the native app-window-wrapper proof:
PYTHONDONTWRITEBYTECODE=1 python3 scripts/wayland-probe/run.py native-custom-overlay
```

The compiler extracts the original licensed Compose source into the ignored build tree
and changes only `renderSettings = RenderSettings.SkiaSurface()` to
`renderSettings = RenderSettings.SwingGraphics()`. The upstream source explicitly says
this rendering mode is not intended for standalone windows. This is an unsupported
**proof of concept**, not a supported runtime flag. The original app jars are not changed;
only named overlay cases prepend the compiled class to the smoke process's classpath.

The runner removes `DISPLAY` in native cases, selects `WLToolkit`, sets scratch HOME/XDG
locations, records Wayland protocol logs and library maps, and bounds each process's
lifetime. It does not globally inject mouse/keyboard input or configure the compositor.

Expected baseline:

| Case | Expected exit |
| --- | --- |
| `native-panel-software` | 0 |
| `native-window-software` | 2: known DrawingSurface failure |
| `native-window-swing-flag` | 2: same failure; panel flag does not affect windows |
| `x11-window-control` | 0 |
| `native-window-overlay` | 0 |
| `native-custom-overlay` | 0 |

The runner exits nonzero on unexpected outcomes, including if a formerly failing case
starts succeeding; inspect the logs before updating expectations. A successful result
means basic drawing/lifecycle checks, **not** full native input, animation, scaling,
accessibility, or integration validation. Draw callback counts are not performance data.

Read `HANDOFF-native-wayland.md` before expanding this experiment. In particular, never
reinterpret a Wayland native handle as an XID, and keep the working XWayland path intact.
