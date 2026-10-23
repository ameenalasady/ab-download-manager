#!/usr/bin/env python3
"""Bounded FULL-APP native/XWayland tests with fresh owned profiles, never normal CLI/installers."""
import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import tempfile
import threading
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / 'build/native-wayland'


PAYLOAD = bytes(range(256)) * 4096  # 1 MiB, served locally, never fetch a public test URL
PAYLOAD_SHA256 = hashlib.sha256(PAYLOAD).hexdigest()


class FixtureHandler(BaseHTTPRequestHandler):
    requests = []
    fixture_path = '/abdm-native-smoke/' + uuid.uuid4().hex + '/payload.bin'

    def do_HEAD(self):
        self.serve(False)

    def do_GET(self):
        self.serve(True)

    def serve(self, body):
        if self.path != self.fixture_path:
            self.send_error(404)
            return
        start, end = 0, len(PAYLOAD) - 1
        requested = self.headers.get('Range')
        if requested:
            bounds = requested.removeprefix('bytes=').split('-')
            start = int(bounds[0])
            end = min(int(bounds[1]) if bounds[1] else end, end)
        if not (0 <= start <= end < len(PAYLOAD)):
            self.send_error(416)
            return
        self.requests.append({'method': self.command, 'range': requested})
        self.send_response(206 if requested else 200)
        self.send_header('Content-Length', str(end - start + 1))
        self.send_header('Accept-Ranges', 'bytes')
        self.send_header('Content-Type', 'application/octet-stream')
        if requested:
            self.send_header('Content-Range', f'bytes {start}-{end}/{len(PAYLOAD)}')
        self.end_headers()
        if body:
            self.wfile.write(PAYLOAD[start:end + 1])

    def log_message(self, *args):
        pass


def protected_snapshot():
    home = Path.home()
    files = set((home / '.abdm/config').rglob('*'))
    for rel in ['.local/share/applications/com.abdownloadmanager.desktop',
                '.config/autostart/com.abdownloadmanager.desktop']:
        files.add(home / rel)
    files.update((home / '.local/share/kwin-wayland/effects/maximize').rglob('*'))
    for directory in ['.mozilla/native-messaging-hosts', '.config/google-chrome/NativeMessagingHosts',
                      '.config/chromium/NativeMessagingHosts', '.config/BraveSoftware/Brave-Browser/NativeMessagingHosts',
                      '.config/microsoft-edge/NativeMessagingHosts', '.config/vivaldi/NativeMessagingHosts',
                      '.config/opera/NativeMessagingHosts']:
        files.update((home / directory).glob('*abdownloadmanager*'))
    return {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(files) if p.is_file()}


def listening_ports(pid):
    inodes = set()
    for fd in Path(f'/proc/{pid}/fd').glob('*'):
        try:
            target = os.readlink(fd)
            if target.startswith('socket:['):
                inodes.add(target[8:-1])
        except FileNotFoundError:
            pass
    ports = set()
    for protocol in ['tcp', 'tcp6']:
        try:
            lines = Path(f'/proc/{pid}/net/{protocol}').read_text().splitlines()[1:]
        except FileNotFoundError:
            continue
        for line in lines:
            columns = line.split()
            if columns[3] == '0A' and columns[9] in inodes:
                ports.add(int(columns[1].split(':')[1], 16))
    return ports


def stop(process):
    if process.poll() is None:
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()


def reopen_session(launcher, backend, profile):
    """Reopen saved state; second launch and quit must reach only this profile's IPC."""
    log = OUTPUT / (profile.name + '-reopen.log')
    result = {'errors': [], 'log': str(log)}
    base = [str(launcher), '--backend', backend, '--profile', str(profile)]
    with log.open('w') as out:
        process = subprocess.Popen(base, stdout=out, stderr=subprocess.STDOUT, start_new_session=True, cwd=ROOT)
        try:
            deadline = time.monotonic() + 35
            record = {}
            while time.monotonic() < deadline and process.poll() is None:
                try:
                    record = json.loads((profile / 'session-status.json').read_text())
                    if record.get('pid') == process.pid and record.get('stage') == 'ready':
                        break
                except (OSError, ValueError):
                    pass
                time.sleep(0.1)
            if record.get('pid') != process.pid or record.get('loadedDownloads') != 1:
                raise RuntimeError('reopen did not load the persisted completed download')
            result['status'] = record
            second = subprocess.run(base, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    text=True, timeout=20, start_new_session=True, cwd=ROOT)
            (OUTPUT / (profile.name + '-second-launch.log')).write_text(second.stdout)
            if second.returncode != 0 or 'instance already running' not in second.stdout or process.poll() is not None:
                raise RuntimeError('second launch did not address the existing isolated instance')
            result['second_launch_exit'] = second.returncode
            quit_process = subprocess.run([*base, '--quit'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                          text=True, timeout=20, start_new_session=True, cwd=ROOT)
            (OUTPUT / (profile.name + '-quit.log')).write_text(quit_process.stdout)
            result['quit_exit'] = quit_process.returncode
            if quit_process.returncode != 0 or process.wait(timeout=15) != 0:
                raise RuntimeError('isolated IPC quit failed')
            if (profile / 'app-data/config/app.port').exists():
                raise RuntimeError('profile IPC port file not cleaned on exit')
        except (RuntimeError, subprocess.TimeoutExpired) as error:
            result['errors'].append(str(error))
        finally:
            stop(process)
    if 'Exception in thread' in log.read_text(errors='replace'):
        result['errors'].append('uncaught exception during reopened-session shutdown')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backend', choices=['wayland', 'x11', 'both'], default='both')
    parser.add_argument('--bundle', type=Path, default=OUTPUT / 'latest', help='Also supports testing a relocated archive extraction')
    args = parser.parse_args()
    launcher = args.bundle.resolve() / 'bin/ABDownloadManagerNativeWayland'
    if not launcher.is_file():
        parser.error('Build the experimental bundle first')
    OUTPUT.mkdir(exist_ok=True)
    before = protected_snapshot()
    (OUTPUT / 'before-full-app-protected-hashes.json').write_text(json.dumps(before, indent=2) + '\n')
    current = Path.home() / '.local/opt/ab-download-manager-plasma/current'
    installed_release = str(current.resolve())
    installed_pattern = '^' + re.escape(str(current / 'bin/ABDownloadManager')) + r'(\s|$)'
    installed_pids = subprocess.run(['pgrep', '-af', installed_pattern],
                                    stdout=subprocess.PIPE, text=True).stdout
    results = []
    server = ThreadingHTTPServer(('127.0.0.1', 0), FixtureHandler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f'http://127.0.0.1:{server.server_port}{FixtureHandler.fixture_path}'
    try:
        # Non-window entrypoints must not force AWT initialization (a normal CLI regression).
        cli_profile = Path(tempfile.mkdtemp(prefix='cli-without-display-', dir=OUTPUT))
        cli_env = os.environ.copy()
        cli_env.update(DISPLAY=':65530', WAYLAND_DISPLAY='abdm-does-not-exist')
        cli = subprocess.run([str(launcher), '--backend', 'x11', '--profile', str(cli_profile), '--quit'],
                             env=cli_env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             text=True, timeout=20, start_new_session=True, cwd=ROOT)
        (OUTPUT / (cli_profile.name + '.log')).write_text(cli.stdout)
        if cli.returncode != 0 or (cli_profile / 'session-status.json').exists():
            raise RuntimeError('Non-window CLI unexpectedly needs a live AWT/display backend')
        print('Non-window CLI with deliberately unavailable displays: PASS (owned empty profile only).')
        for backend in ['wayland', 'x11'] if args.backend == 'both' else [args.backend]:
            profile = Path(tempfile.mkdtemp(prefix=f'full-app-{backend}-', dir=OUTPUT))
            # Launcher rejects a nonempty unowned directory; keep the log outside until marked.
            log = OUTPUT / (profile.name + '.log')
            ports, maps = set(), ''
            with log.open('w') as out:
                request_count = len(FixtureHandler.requests)
                process = subprocess.Popen([str(launcher), '--backend', backend, '--profile', str(profile), '--smoke-test',
                                            '--smoke-download-url', url, '--smoke-download-sha256', PAYLOAD_SHA256],
                                           stdout=out, stderr=subprocess.STDOUT, start_new_session=True, cwd=ROOT)
                try:
                    deadline = time.monotonic() + 80
                    while process.poll() is None and time.monotonic() < deadline:
                        try:
                            snapshot = Path(f'/proc/{process.pid}/maps').read_text()
                            if 'libawt_' in snapshot:
                                maps = snapshot
                            ports.update(listening_ports(process.pid))
                        except (FileNotFoundError, ProcessLookupError):
                            pass
                        time.sleep(0.2)
                    code = process.poll()
                    if code is None:
                        stop(process)
                        code = 124
                finally:
                    stop(process)
            (OUTPUT / (profile.name + '.maps')).write_text(maps)
            text = log.read_text(errors='replace')
            errors = []
            if code != 0:
                errors.append(f'exit={code}')
            for failure in ['Exception in thread', 'Error was captured in composition', "Can't lock DrawingSurface",
                            'Swing hierarchy should be accessed only from AWT Event Dispatch Thread']:
                if failure in text:
                    errors.append('uncaught GUI/shutdown failure: ' + failure)
            record_file = profile / 'session-status.json'
            record = json.loads(record_file.read_text()) if record_file.exists() else {}
            expected = 'sun.awt.wl.WLToolkit' if backend == 'wayland' else 'sun.awt.X11.XToolkit'
            if record.get('stage') != 'smoke_complete' or record.get('toolkit') != expected:
                errors.append('missing full-app completion/backend evidence')
            if record.get('runtime') != '25.0.4.1+1-b635.72':
                errors.append('unexpected bundled runtime')
            if record.get('dataDir') != str(profile / 'app-data'):
                errors.append('application data not explicitly isolated')
            if record.get('integrationPort') != -1 or record.get('tray') is not False or record.get('updates') is not False:
                errors.append('integration/tray/update policy not enforced')
            ui = 'ui-desktop-1.12.1-abdm-native-experimental.jar'
            if ui not in record.get('uiSource', ''):
                errors.append('wrong Compose UI code source')
            # Only the independent single-instance RPC listener is allowed, never browser integration.
            ipc = record.get('ipcPort')
            if not ipc or ports != {ipc} or ipc == 15151:
                errors.append(f'unexpected process-owned listeners: {sorted(ports)}; IPC={ipc}')
            if backend == 'wayland':
                if 'libawt_wlawt.so' not in maps or 'libawt_xawt.so' in maps:
                    errors.append('missing native AWT library evidence')
                for evidence in ['.attach(wl_buffer#', '.commit()', 'NativeWaylandAppKt']:
                    if evidence not in text:
                        errors.append('missing Wayland evidence: ' + evidence)
            if any(profile.rglob('*.desktop')) or any(profile.rglob('native-messaging-hosts')) or any(profile.rglob('NativeMessagingHosts')):
                errors.append('desktop/autostart/native messaging artifacts created')
            payloads = list((profile / 'home/Downloads').glob('full-app-payload*'))
            if (record.get('downloadSha256') != PAYLOAD_SHA256 or len(payloads) != 1 or
                    hashlib.sha256(payloads[0].read_bytes()).hexdigest() != PAYLOAD_SHA256 or
                    len(FixtureHandler.requests) == request_count):
                errors.append('real engine download/checksum/HTTP evidence missing')
            settings = profile / 'app-data/config/appSettings.json'
            if not settings.is_file():
                errors.append('missing persisted app settings')
            else:
                saved = json.loads(settings.read_text())
                if any(saved.get(key) is not False for key in ['apiEnabled', 'autoStartOnBoot', 'useSystemTray']):
                    errors.append('preferences were not flushed on exit')
            reopened = reopen_session(launcher, backend, profile) if not errors else {}
            errors += reopened.get('errors', [])
            result = {'backend': backend, 'exit': code, 'errors': errors, 'profile': str(profile),
                      'log': str(log), 'listening_ports': sorted(ports), 'status': record, 'reopen': reopened}
            results.append(result)
            print(json.dumps(result, indent=2))
    finally:
        server.shutdown()
        server.server_close()
        after = protected_snapshot()
        (OUTPUT / 'after-full-app-protected-hashes.json').write_text(json.dumps(after, indent=2) + '\n')
        if before != after:
            raise RuntimeError('Protected production files changed during the test; inspect hash snapshots (never auto-restore)')
        assert str(current.resolve()) == installed_release
        assert subprocess.run(['pgrep', '-af', installed_pattern],
                              stdout=subprocess.PIPE, text=True).stdout == installed_pids
        print('Protected production stores/integration files, installed release and PID unchanged.')
        (OUTPUT / 'full-app-results.json').write_text(json.dumps(results, indent=2) + '\n')
    raise SystemExit(1 if any(r['errors'] for r in results) else 0)


if __name__ == '__main__':
    main()
