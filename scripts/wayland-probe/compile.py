#!/usr/bin/env python3
"""Compile the isolated native-Wayland probe, never the ABDM entrypoint.

Requires a previously built plasma.2 app distribution, cached Kotlin 2.4.20 compilers,
and (for --overlay) the pinned Compose source jar described in README.md.
All generated files stay under ignored build/wayland-probe/.
"""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
PROBE = ROOT / 'build/wayland-probe'
APP = ROOT / 'desktop/app/build/compose/binaries/main-release/app/ABDownloadManager/lib/app'
CACHE = Path.home() / '.gradle/caches/modules-2/files-2.1/org.jetbrains.kotlin'
VERSION = '2.4.20'
SOURCE_SHA256 = 'ae1ccb98d6d7335c7e2d0ec5024e6cda800d1737bab3477f81c45914e27b786e'
SOURCE_ENTRY = 'desktopMain/androidx/compose/ui/awt/ComposeWindowPanel.desktop.kt'


def cached_jar(module):
    jars = list((CACHE / module / VERSION).rglob('*.jar'))
    if len(jars) != 1:
        raise SystemExit(f'Expected one cached {module} {VERSION}; build the app with Java 25 first.')
    return jars[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--overlay', action='store_true',
                        help='Compile the unsupported scratch-only Compose rendering override')
    args = parser.parse_args()
    compiler = cached_jar('kotlin-compiler-embeddable')
    plugin = cached_jar('kotlin-compose-compiler-plugin-embeddable')
    ui_jars = list(APP.glob('ui-desktop-1.12.1-*.jar'))
    if len(ui_jars) != 1:
        parser.error('Expected the existing Compose 1.12.1 packaged app; see README.md.')
    cp = os.pathsep.join(str(p) for p in sorted(APP.glob('*.jar')))
    PROBE.mkdir(parents=True, exist_ok=True)
    out = PROBE / ('overlay-classes' if args.overlay else 'smoke-classes')
    out.mkdir(exist_ok=True)
    source = Path(__file__).with_name('WaylandSmoke.kt')
    if args.overlay:
        archive = PROBE / 'ui-sources.jar'
        if not archive.is_file() or hashlib.sha256(archive.read_bytes()).hexdigest() != SOURCE_SHA256:
            parser.error('Missing or unexpected Compose source archive; see README.md for the pinned download.')
        with zipfile.ZipFile(archive) as jar:
            original = jar.read(SOURCE_ENTRY).decode()
        old = 'renderSettings = RenderSettings.SkiaSurface(),'
        if original.count(old) != 1:
            parser.error('Upstream source does not match the reviewed experiment; refusing to patch it.')
        source = PROBE / 'overlay-src/ComposeWindowPanel.desktop.kt'
        source.parent.mkdir(exist_ok=True)
        source.write_text(original.replace(old, 'renderSettings = RenderSettings.SwingGraphics(),'))
    cmd = ['/usr/lib/jvm/java-25-openjdk/bin/java', '-cp', str(compiler) + os.pathsep + cp,
           'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler', '-no-stdlib', '-no-reflect', '-jvm-target', '25',
           f'-Xplugin={plugin}', '-classpath', cp, '-d', str(out)]
    if args.overlay:
        cmd.extend([f'-Xfriend-paths={ui_jars[0]}',
                    '-opt-in=androidx.compose.ui.ExperimentalComposeUiApi',
                    '-opt-in=androidx.compose.ui.InternalComposeUiApi'])
    cmd.append(str(source))
    run = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (PROBE / ('overlay-compile.log' if args.overlay else 'smoke-compile.log')).write_text(run.stdout)
    print(run.stdout)
    if run.returncode:
        raise SystemExit(run.returncode)
    if not args.overlay:
        (PROBE / 'runtime-classpath.txt').write_text(str(out) + os.pathsep + cp)
    print('Compiled:', source.name)


if __name__ == '__main__':
    main()
