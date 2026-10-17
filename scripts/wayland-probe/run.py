#!/usr/bin/env python3
"""Run isolated native/control window tests, never ABDM's entrypoint or launcher.

Native cases require WLToolkit and protocol evidence; only the explicitly named mixed-env
case retains DISPLAY to test backend routing. No global input or compositor changes.
"""
import argparse
from dataclasses import dataclass
import json
import os
from pathlib import Path
import signal
import subprocess
import time

import native_ui

ROOT = Path(__file__).resolve().parents[2]
PROBE = ROOT / 'build/wayland-probe'
APP = ROOT / 'desktop/app/build/compose/binaries/main-release/app/ABDownloadManager/lib/app'
ARTIFACT = PROBE / 'ui-desktop-1.12.1-abdm-native-experimental.jar'


@dataclass(frozen=True)
class Case:
    name: str
    toolkit: str = 'WLToolkit'
    mode: str = 'window'
    renderer: str = 'stock'
    opt_in: bool = False
    keep_display: bool = False
    options: tuple = ()
    expected: int = 0
    runtime: str = 'jbr'


CASES = [
    Case('native-panel-software', mode='panel'),
    Case('native-window-software', expected=2),
    Case('native-window-swing-flag', options=('-Dcompose.swing.render.on.graphics=true',), expected=2),
    Case('x11-window-control', toolkit='XToolkit'),
    Case('native-window-overlay', renderer='overlay'),
    Case('native-custom-overlay', mode='custom', renderer='overlay'),
    Case('native-window-experimental', renderer='experimental', opt_in=True),
    Case('native-custom-experimental', mode='custom', renderer='experimental', opt_in=True),
    Case('native-custom-experimental-with-display', mode='custom', renderer='experimental',
         opt_in=True, keep_display=True),
    Case('native-dialog-experimental', mode='dialog', renderer='experimental', opt_in=True),
    Case('native-window-experimental-without-opt-in', renderer='experimental', expected=2),
    Case('x11-window-experimental-control', toolkit='XToolkit', renderer='experimental', opt_in=True),
    Case('x11-custom-control', toolkit='XToolkit', mode='custom'),
    Case('openjdk25-x11-custom-control', toolkit='XToolkit', mode='custom', runtime='openjdk25'),
]


def stop(process):
    if process.poll() is None:
        os.killpg(process.pid, signal.SIGTERM)
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()


def evidence_errors(case, code, text, maps, ui_source, display):
    """Exit status alone cannot distinguish the intended native result from another failure."""
    errors = []
    runtime_marker = 'runtime=25' if case.runtime == 'openjdk25' else 'runtime=25.0.4.1+1-b635.72'
    toolkit_class = 'sun.awt.wl.WLToolkit' if case.toolkit == 'WLToolkit' else 'sun.awt.X11.XToolkit'
    required = [runtime_marker, f'toolkit={toolkit_class}',
                f'composeWindowPanelSource={ui_source}',
                f'nativeRenderingOptIn={str(case.opt_in).lower()}']
    if code != case.expected:
        errors.append(f'exit {code}, expected {case.expected}')
    if case.toolkit == 'WLToolkit':
        required += ['awtBackend=Wayland', 'nativeX11Guard=true xOpenDisplayAttempted=false',
                     f'DISPLAY={display if case.keep_display else "null"}']
    else:
        required += ['awtBackend=X11']
    if case.expected == 2:
        required += ["Can't lock DrawingSurface", 'SMOKE_FAILED']
    else:
        marker = {'custom': 'CUSTOM_SMOKE_COMPLETE', 'dialog': 'DIALOG_SMOKE_COMPLETE'}.get(case.mode, 'SMOKE_COMPLETE')
        required.append(marker)
        if case.mode == 'custom':
            required += ['customContentDisposed=true', 'customMaximizedAfterVeto=6',
                         'customRestored=0', 'customClosingMaximized=6']
        if 'SMOKE_FAILED' in text:
            errors.append('uncaught exception in success case')
        if case.toolkit == 'WLToolkit':
            required += ['.attach(wl_buffer#', '.commit()']
            if 'libawt_wlawt.so' not in maps or 'libawt_xawt.so' in maps:
                errors.append('missing native AWT library evidence or XToolkit library loaded')
    for needle in required:
        if needle not in text:
            errors.append(f'missing evidence: {needle}')
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cases', nargs='*', help='Run only named cases (default: the full matrix)')
    args = parser.parse_args()
    unknown = set(args.cases) - {case.name for case in CASES}
    if unknown:
        parser.error(f'Unknown cases: {sorted(unknown)}')
    selected = [c for c in CASES if not args.cases or c.name in args.cases]
    runtime = PROBE / 'runtime/bin/java'
    cp_file = PROBE / 'runtime-classpath.txt'
    if not runtime.is_file() or not cp_file.is_file():
        parser.error('Prepare the scratch runtime and compile the probe first; see README.md.')
    cp = cp_file.read_text().split(os.pathsep)
    if any(Path(p).name == 'pathing-classpath.jar' for p in cp):
        parser.error('Redundant manifest classpath can load the stock UI; recompile the probe.')
    if not all(Path(p).exists() for p in cp):
        parser.error('Stale runtime classpath; rebuild the distributable and recompile the probe.')
    ui = [p for p in cp if Path(p).name.startswith('ui-desktop-1.12.1-')]
    if len(ui) != 1:
        parser.error('Expected exactly one pinned UI jar in the probe classpath.')
    if any(c.toolkit == 'WLToolkit' for c in selected) and not os.environ.get('WAYLAND_DISPLAY'):
        parser.error('An accessible WAYLAND_DISPLAY is required; no headless/X11 substitution.')
    if any(c.toolkit == 'XToolkit' or c.keep_display for c in selected) and not os.environ.get('DISPLAY'):
        parser.error('X11/mixed-environment controls require an existing DISPLAY.')
    if any(c.renderer == 'experimental' for c in selected):
        try:
            manifest = json.loads((PROBE / 'native-ui-manifest.json').read_text())
            native_ui.validate_artifact(Path(ui[0]), ARTIFACT, manifest)
        except (OSError, ValueError) as error:
            parser.error(f'{error}; run compile.py --native-ui first.')
    if any(c.renderer == 'overlay' for c in selected) and not (PROBE / 'overlay-classes').is_dir():
        parser.error('Compile the historical scratch overlay with compile.py --overlay first.')
    system_java = Path('/usr/lib/jvm/java-25-openjdk/bin/java')
    if any(c.runtime == 'openjdk25' for c in selected) and not system_java.is_file():
        parser.error('The OpenJDK25 fallback control requires /usr/lib/jvm/java-25-openjdk/bin/java.')
    results = []
    for case in selected:
        java = system_java if case.runtime == 'openjdk25' else runtime
        isolation = PROBE / 'cases' / case.name
        for name in ['home', 'config', 'data', 'cache']:
            (isolation / name).mkdir(parents=True, exist_ok=True)
        env = os.environ.copy()
        for name in ['JAVA_TOOL_OPTIONS', 'JDK_JAVA_OPTIONS', '_JAVA_OPTIONS', 'WAYLAND_DEBUG']:
            env.pop(name, None)
        env.update(HOME=str(isolation / 'home'), XDG_CONFIG_HOME=str(isolation / 'config'),
                   XDG_DATA_HOME=str(isolation / 'data'), XDG_CACHE_HOME=str(isolation / 'cache'),
                   SKIKO_RENDER_API='SOFTWARE')
        if case.toolkit == 'WLToolkit':
            if not case.keep_display:
                env.pop('DISPLAY', None)
            env['WAYLAND_DEBUG'] = '1'
        case_cp = cp.copy()
        source = Path(ui[0])
        if case.renderer == 'experimental':
            # Replace the dependency, do not prepend duplicate override classes.
            case_cp[case_cp.index(ui[0])] = str(ARTIFACT)
            source = ARTIFACT
        elif case.renderer == 'overlay':
            source = PROBE / 'overlay-classes'
            case_cp.insert(0, str(source))
        # Java URL.toString uses file:/ rather than pathlib's file:///.
        ui_source = 'file:' + str(source) + ('/' if source.is_dir() else '')
        cmd = [str(java), f'-Dawt.toolkit.name={case.toolkit}', '--enable-native-access=ALL-UNNAMED',
               f'-Duser.home={isolation / "home"}', f'-Dskiko.library.path={APP}',
               f'-Dcompose.application.resources.dir={APP / "resources"}',
               '-Dabdm.systemUiScale=auto',
               f'-Dabdm.experimental.nativeWayland={str(case.opt_in).lower()}', *case.options,
               '-cp', os.pathsep.join(case_cp), 'WaylandSmokeKt', case.mode]
        log = PROBE / f'{case.name}.log'
        maps = ''
        with log.open('w') as out:
            process = subprocess.Popen(cmd, env=env, stdout=out, stderr=subprocess.STDOUT,
                                       cwd=isolation, start_new_session=True)
            try:
                deadline = time.monotonic() + 25
                while process.poll() is None and time.monotonic() < deadline:
                    try:
                        snapshot = Path(f'/proc/{process.pid}/maps').read_text()
                        if 'libawt_' in snapshot or not maps:
                            maps = snapshot
                    except (FileNotFoundError, ProcessLookupError):
                        pass
                    time.sleep(0.2)
                code = process.poll()
                if code is None:
                    stop(process)
                    code = 124
            finally:
                stop(process)
        (PROBE / f'{case.name}.maps').write_text(maps)
        text = log.read_text(errors='replace')
        errors = evidence_errors(case, code, text, maps, ui_source, os.environ.get('DISPLAY'))
        result = {'name': case.name, 'exit': code, 'expected': case.expected,
                  'errors': errors, 'log': str(log.relative_to(ROOT))}
        results.append(result)
        print(json.dumps(result))
        for line in text.splitlines():
            if any(s in line for s in ['runtime=', 'toolkit=', 'DISPLAY=', 'mode=', 'SMOKE_',
                                      'Requested=', 'Observed=', 'windowOpened=', 'windowState=',
                                      'custom', 'dialog', 'jbrDecorations=', 'jbrWindowMove=', 'ewmhMove=',
                                      'composeWindowPanelSource=', 'composeDensity=', 'screen=',
                                      'nativeX11Guard=', 'awtBackend=', 'nativeRenderingOptIn=',
                                      'observedWindowHandle=', 'Exception', 'Error']):
                print(line)
    summary = PROBE / 'smoke-results.json'
    previous = {r['name']: r for r in json.loads(summary.read_text())} if summary.exists() else {}
    previous.update({r['name']: r for r in results})
    summary.write_text(json.dumps(list(previous.values()), indent=2) + '\n')
    raise SystemExit(1 if any(r['errors'] for r in results) else 0)


if __name__ == '__main__':
    main()
