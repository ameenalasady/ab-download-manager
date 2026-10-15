#!/usr/bin/env python3
"""Run isolated native/control smoke cases, never ABDM's launcher or integration code.

Requires the scratch runtime and compiled probe described in README.md. Native cases
remove DISPLAY so they cannot silently fall back to XWayland. Windows live for a few
seconds; no global input is injected. No installer or compositor configuration is touched.
"""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
PROBE = ROOT / 'build/wayland-probe'
APP = ROOT / 'desktop/app/build/compose/binaries/main-release/app/ABDownloadManager/lib/app'
CASES = [
    ('native-panel-software', 'WLToolkit', 'panel', [], 0),
    ('native-window-software', 'WLToolkit', 'window', [], 2),
    ('native-window-swing-flag', 'WLToolkit', 'window', ['-Dcompose.swing.render.on.graphics=true'], 2),
    ('x11-window-control', 'XToolkit', 'window', [], 0),
    ('native-window-overlay', 'WLToolkit', 'window', [], 0),
    ('native-custom-overlay', 'WLToolkit', 'custom', [], 0),
]


def stop(process):
    if process.poll() is None:
        os.killpg(process.pid, signal.SIGTERM)
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cases', nargs='*', help='Run only named cases (default: the full matrix)')
    args = parser.parse_args()
    unknown = set(args.cases) - {case[0] for case in CASES}
    if unknown:
        parser.error(f'Unknown cases: {sorted(unknown)}')
    runtime = PROBE / 'runtime/bin/java'
    cp_file = PROBE / 'runtime-classpath.txt'
    if not runtime.is_file() or not cp_file.is_file():
        parser.error('Prepare the scratch runtime and compile the probe first; see README.md.')
    cp = cp_file.read_text()
    for name in ['home', 'config', 'data', 'cache']:
        (PROBE / name).mkdir(parents=True, exist_ok=True)
    results = []
    for name, toolkit, mode, options, expected in CASES:
        if args.cases and name not in args.cases:
            continue
        if 'overlay' in name and not (PROBE / 'overlay-classes').is_dir():
            parser.error('Compile the scratch overlay with compile.py --overlay first.')
        case_cp = str(PROBE / 'overlay-classes') + os.pathsep + cp if 'overlay' in name else cp
        env = os.environ.copy()
        env.update(HOME=str(PROBE / 'home'), XDG_CONFIG_HOME=str(PROBE / 'config'),
                   XDG_DATA_HOME=str(PROBE / 'data'), XDG_CACHE_HOME=str(PROBE / 'cache'),
                   SKIKO_RENDER_API='SOFTWARE')
        if toolkit == 'WLToolkit':
            env.pop('DISPLAY', None)
            env['WAYLAND_DEBUG'] = '1'
        else:
            env.pop('WAYLAND_DEBUG', None)
        cmd = [str(runtime), f'-Dawt.toolkit.name={toolkit}', '--enable-native-access=ALL-UNNAMED',
               f'-Duser.home={PROBE / "home"}', f'-Dskiko.library.path={APP}',
               f'-Dcompose.application.resources.dir={APP / "resources"}', *options,
               '-cp', case_cp, 'WaylandSmokeKt', mode]
        log = PROBE / f'{name}.log'
        with log.open('w') as out:
            process = subprocess.Popen(cmd, env=env, stdout=out, stderr=subprocess.STDOUT,
                                       cwd=PROBE, start_new_session=True)
            try:
                time.sleep(1)
                try:
                    maps = Path(f'/proc/{process.pid}/maps').read_text()
                except FileNotFoundError:
                    maps = 'Process exited before library sampling.\n'
                (PROBE / f'{name}.maps').write_text(maps)
                try:
                    code = process.wait(timeout=25)
                except subprocess.TimeoutExpired:
                    stop(process)
                    code = 124
            finally:
                stop(process)
        result = {'name': name, 'exit': code, 'expected': expected,
                  'log': str(log.relative_to(ROOT))}
        results.append(result)
        print(json.dumps(result))
        for line in log.read_text(errors='replace').splitlines():
            if any(s in line for s in ['runtime=', 'toolkit=', 'DISPLAY=', 'mode=', 'SMOKE_',
                                      'Requested=', 'Observed=', 'windowOpened=', 'windowState=',
                                      'custom', 'jbrDecorations=', 'jbrWindowMove=', 'ewmhMove=',
                                      'composeWindowPanelSource=', 'composeDensity=', 'screen=',
                                      'Exception', 'Error']):
                print(line)
    summary = PROBE / 'smoke-results.json'
    previous = {r['name']: r for r in json.loads(summary.read_text())} if summary.exists() else {}
    previous.update({r['name']: r for r in results})
    summary.write_text(json.dumps(list(previous.values()), indent=2) + '\n')
    # Stock native windows currently fail as documented; unexpected outcomes must be reviewed.
    raise SystemExit(1 if any(r['exit'] != r['expected'] for r in results) else 0)


if __name__ == '__main__':
    main()
