#!/usr/bin/env python3
"""Compile the isolated native-Wayland probe, never the ABDM entrypoint.

Requires a previously built plasma.2 app distribution, cached Kotlin 2.4.20 compilers,
and (for --overlay/--native-ui) the pinned Compose source jar described in README.md.
All generated files stay under ignored build/wayland-probe/.
"""
import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path
import subprocess
import zipfile

import native_ui

ROOT = Path(__file__).resolve().parents[2]
PROBE = ROOT / 'build/wayland-probe'
APP = ROOT / 'desktop/app/build/compose/binaries/main-release/app/ABDownloadManager/lib/app'
CACHE = Path.home() / '.gradle/caches/modules-2/files-2.1/org.jetbrains.kotlin'
VERSION = '2.4.20'
SOURCE_SHA256 = native_ui.SOURCE_SHA256
SOURCE_ENTRY = native_ui.SOURCE_ENTRY


def cached_jar(module):
    jars = list((CACHE / module / VERSION).rglob('*.jar'))
    if len(jars) != 1:
        raise SystemExit(f'Expected one cached {module} {VERSION}; build the app with Java 25 first.')
    return jars[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    rendering = parser.add_mutually_exclusive_group()
    rendering.add_argument('--overlay', action='store_true',
                           help='Compile the original unsupported scratch-only rendering override')
    rendering.add_argument('--native-ui', action='store_true',
                           help='Build a pinned UI jar with explicit native rendering opt-in (experimental)')
    args = parser.parse_args()
    compiler = cached_jar('kotlin-compiler-embeddable')
    plugin = cached_jar('kotlin-compose-compiler-plugin-embeddable')
    ui_jars = list(APP.glob('ui-desktop-1.12.1-*.jar'))
    if len(ui_jars) != 1:
        parser.error('Expected the existing Compose 1.12.1 packaged app; see README.md.')
    # The launcher's pathing jar has a manifest Class-Path back to every ORIGINAL jar.
    # With an explicit classpath it is redundant and can resolve the unpatched UI first.
    cp = os.pathsep.join(map(str, native_ui.explicit_classpath_jars(APP)))
    PROBE.mkdir(parents=True, exist_ok=True)
    out = PROBE / ('native-ui-classes' if args.native_ui else
                   'overlay-classes' if args.overlay else 'smoke-classes')
    # Never let obsolete generated inner classes leak into a new artifact.
    if out.exists():
        shutil.rmtree(out)
    out.mkdir()
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
    if args.native_ui:
        native_ui.require_hash(ui_jars[0], native_ui.BINARY_SHA256)
        source = PROBE / 'native-ui-src/ComposeWindowPanel.desktop.kt'
        native_ui.prepare_source(PROBE / 'ui-sources.jar', source)
    cmd = ['/usr/lib/jvm/java-25-openjdk/bin/java', '-cp', str(compiler) + os.pathsep + cp,
           'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler', '-no-stdlib', '-no-reflect', '-jvm-target', '25',
           f'-Xplugin={plugin}', '-classpath', cp, '-d', str(out)]
    if args.overlay or args.native_ui:
        cmd.extend([f'-Xfriend-paths={ui_jars[0]}',
                    '-opt-in=androidx.compose.ui.ExperimentalComposeUiApi',
                    '-opt-in=androidx.compose.ui.InternalComposeUiApi'])
    else:
        app_utils = list(APP.glob('app-utils-*.jar'))
        if len(app_utils) != 1:
            parser.error('Expected one current app-utils jar; rebuild the distributable without installing.')
        cmd.append(f'-Xfriend-paths={app_utils[0]}')
    cmd.append(str(source))
    run = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (PROBE / ('native-ui-compile.log' if args.native_ui else
              'overlay-compile.log' if args.overlay else 'smoke-compile.log')).write_text(run.stdout)
    print(run.stdout)
    if run.returncode:
        raise SystemExit(run.returncode)
    if args.native_ui:
        artifact = PROBE / 'ui-desktop-1.12.1-abdm-native-experimental.jar'
        manifest = native_ui.build_jar(ui_jars[0], out, artifact)
        (PROBE / 'native-ui-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print('Experimental artifact:', artifact.relative_to(ROOT))
    elif not args.overlay:
        (PROBE / 'runtime-classpath.txt').write_text(str(out) + os.pathsep + cp)
    print('Compiled:', source.name)


if __name__ == '__main__':
    main()
