# Isolated native Wayland smoke tests

**Experimental only. Do not use this as an installer or modify the normal launcher.**

These started as the retained feasibility tests in `WAYLAND-FEASIBILITY.md` and now also
exercise backend-safe routing and an explicit, pinned experimental rendering artifact.
See [`../../NATIVE-WAYLAND.md`](../../NATIVE-WAYLAND.md) for implementation status.
They use the packaged ABDM libraries and its real `CustomWindow` wrapper, but never start
ABDM's entrypoint, download engine, data stores, tray, or integration server.
For the separate **complete app bundle** (with an audited isolated profile and bundled JBR),
use [`../native-wayland/README.md`](../native-wayland/README.md), not this window-only main.

All runtime downloads, generated overlay source/classes, logs, and test home/config/data
stay in ignored `build/wayland-probe/`. Nothing under that directory belongs in Git.
The original local scratch scripts remain there as historical artifacts; use the scripts
in **this directory** going forward, since they survive a fresh clone.

## Requirements

- Linux with an accessible Wayland/KWin session; an XWayland `DISPLAY` for the control case.
- Java 25 at `/usr/lib/jvm/java-25-openjdk` (adjust `compile.py` if using another location).
- Python 3, curl, tar, GNU patch; Kotlin/Compose compiler 2.4.20 cached by the normal Gradle build.
- Packaged app artifacts with Compose 1.12.1 and Skiko 0.150.1. These probes deliberately
  pin the measured baseline; don't interpret them as a general framework compatibility test.

From the repository root, build artifacts **without installing/restarting the normal app**
if they are not already present:

```sh
JAVA_HOME=/usr/lib/jvm/java-25-openjdk ./gradlew \
  -PlocalAppVersion=1.11.0+plasma.2 \
  :desktop:shared:test :desktop:app-utils:test :desktop:app:createReleaseDistributable
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
PYTHONDONTWRITEBYTECODE=1 python3 scripts/wayland-probe/compile.py --overlay  # historical control only
PYTHONDONTWRITEBYTECODE=1 python3 scripts/wayland-probe/compile.py --native-ui
PYTHONDONTWRITEBYTECODE=1 python3 scripts/wayland-probe/compile.py
PYTHONDONTWRITEBYTECODE=1 python3 scripts/wayland-probe/run.py
# Or just the new native wrapper, mixed-environment guard, and dialog tests:
PYTHONDONTWRITEBYTECODE=1 python3 scripts/wayland-probe/run.py \
  native-custom-experimental native-custom-experimental-with-display native-dialog-experimental
# Dependency artifact and runner-evidence regression tests (no GUI/runtime download needed):
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s scripts/tests -p 'test_wayland_probe.py' -v
```

### Explicit experimental artifact

`--native-ui` verifies both the source archive above and the original UI jar (SHA-256
`eaee42812c81cf1f1a925088bd7e847a343decbc3ccb1682f00f203123a51a17`). It applies the tracked,
reviewable `compose-1.12.1-native-wayland.patch` with zero fuzz, compiles the patched source,
and builds `build/wayland-probe/ui-desktop-1.12.1-abdm-native-experimental.jar`. Only the
`ComposeWindowPanel` class family is replaced in this **new** artifact. Upstream license,
other classes/resources, and Kotlin module metadata are retained; obsolete generated inner
classes are removed. Original packaged jars are never rewritten. Source, binary, patch,
and output digests are recorded in `native-ui-manifest.json` and checked before a run.

Named `experimental` cases **substitute** this dependency in an explicit test classpath;
they do not prepend override classes. The redundant launcher `pathing-classpath.jar` is
excluded: its manifest otherwise resolves the original UI jar first. Code-source checks
in the runner reject that mistake. No Gradle dependency or production launcher selects
the experimental artifact.

The patch selects `RenderSettings.SwingGraphics()` only when BOTH conditions hold:

- `-Dabdm.experimental.nativeWayland=true` (set only by the named opt-in cases), and
- the actual window toolkit class is `sun.awt.wl.WLToolkit`.

Otherwise it retains the original `RenderSettings.SkiaSurface()`. The negative native
case deliberately fails without opt-in; the flagged XToolkit control must still expose a
nonzero native handle. This flag belongs to our experimental patch, **not upstream Compose**.
Standalone Swing rendering remains unsupported upstream, with an extra frame copy and
no Skiko fullscreen/native handle. See the unsupported-feature list in `NATIVE-WAYLAND.md`.

`--overlay` still reproduces the original **unconditional** scratch-only substitution for
historical comparison. Only cases explicitly named `overlay` prepend its classes. It is
not the new implementation artifact and must never be used as a production launch hook.

### Isolation and evidence

The runner selects `WLToolkit` and unsets `DISPLAY` in native cases **except** the explicitly
named `with-display` guard test. That case requires both session variables, still verifies
the actual native toolkit/library/protocol traffic, and verifies that no XOpenDisplay was
attempted even after all low-level X11 helpers are called with a nonzero fake handle.
All native cases check automatic scale compensation is 1.0. Explicit scale overrides have
separate headless tests.

Each case has separate `build/wayland-probe/cases/<name>/{home,config,data,cache}` directories
and `user.home`. Inherited JVM option variables are removed. Only this standalone main runs;
no fixed ABDM port, single-instance negotiation, autostart, tray, manifest installation or
integration broadcast is started. Titles and Wayland app identity identify the probe rather
than the installed app. Logs, sampled library maps, and merged last-case results stay under
`build/wayland-probe/`. Process lifetime is bounded to 25 seconds. No global input injection,
compositor configuration, or installer is used.

Exit status alone is insufficient: the runner checks the requested runtime/toolkit, UI
code source, completion/disposal markers, known error text for negative controls, and
native AWT library plus buffer attach/commit evidence for successful native cases.

Expected matrix (14 cases: 11 successes, 3 deliberate failures):

| Case | Expected exit |
| --- | --- |
| `native-panel-software` | 0 |
| `native-window-software` | 2: known DrawingSurface failure |
| `native-window-swing-flag` | 2: same failure; panel flag does not affect windows |
| `x11-window-control` | 0 |
| `native-window-overlay` | 0 |
| `native-custom-overlay` | 0 |
| `native-window-experimental` | 0 |
| `native-custom-experimental` | 0 |
| `native-custom-experimental-with-display` | 0, no XOpenDisplay attempt |
| `native-dialog-experimental` | 0, modeless dialog veto/resize/dispose |
| `native-window-experimental-without-opt-in` | 2: DrawingSurface failure proves opt-in gating |
| `x11-window-experimental-control` | 0, original rendering despite native opt-in |
| `x11-custom-control` | 0, JBR XToolkit with stock Compose |
| `openjdk25-x11-custom-control` | 0, system OpenJDK25 fallback, EWMH move available on KWin |

The runner exits nonzero on unexpected outcomes, including if a formerly failing case
starts succeeding; inspect the logs before updating expectations. CustomWindow cases now
check resize, vetoed WM-close, maximize/restore, accepted close **from maximized**, and AWT
and content disposal. Close events and placement requests are still programmatic. Dialog
coverage is modeless; popup state changes are timed, not real input.

A successful result means basic drawing/lifecycle checks, **not** real native input,
animations, fullscreen, mixed-DPI correctness, accessibility, integration, or performance.
Draw callback counts are not FPS. Read `HANDOFF-native-wayland.md` before expanding the
experiment. Never reinterpret a Wayland handle as an XID; keep the working fallback intact.
