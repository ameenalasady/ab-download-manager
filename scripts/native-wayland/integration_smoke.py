#!/usr/bin/env python3
"""Real native-host/cold-start/HTTP/tray/autostart checks, entirely disposable integration home."""
import hashlib
from http.server import ThreadingHTTPServer
import importlib.util
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
from urllib.request import Request, urlopen

HERE = Path(__file__).resolve().parent


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, HERE / file)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


activation = load('native_integrated_activation', 'desktop_integration.py')
smoke = load('native_integrated_fixture', 'smoke.py')
ROOT, OUTPUT = activation.ROOT, activation.ROOT / 'build/native-wayland'


class Handler(smoke.FixtureHandler):
    def serve(self, body):
        self.path = self.path.split('?', 1)[0]
        super().serve(body)


def await_condition(condition, description, timeout=40):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            result = condition()
            if result:
                return result
        except (OSError, ValueError):
            pass
        time.sleep(.15)
    raise RuntimeError('Timed out: ' + description)


def request_native(host, action, payload, env=None):
    request = {'id': 'B_native_integration_test', 'content': {'action': action, 'isError': False, 'payload': json.dumps(payload)}}
    encoded = json.dumps(request).encode()
    process = subprocess.run([str(host), 'chrome-extension://test-origin/'],
                             input=struct.pack('<I', len(encoded)) + encoded,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=45, env=env)
    if process.returncode != 0:
        raise RuntimeError('Native host failed: ' + process.stderr.decode(errors='replace'))
    if len(process.stdout) < 4:
        raise RuntimeError('No framed native response: ' + repr(process.stdout))
    size = struct.unpack('<I', process.stdout[:4])[0]
    if len(process.stdout) != size + 4:
        raise RuntimeError('Native stdout contains unframed text or an incomplete response: ' + repr(process.stdout[:160]))
    response = json.loads(process.stdout[4:])
    if response['id'] != request['id'] or response['content']['isError'] or json.loads(response['content']['payload']) is not True:
        raise RuntimeError('Native request failed: ' + json.dumps(response))
    return response


def tray_owner_present(pid):
    response = subprocess.check_output(['busctl', '--user', '--json=short', 'get-property', 'org.kde.StatusNotifierWatcher',
                                        '/StatusNotifierWatcher', 'org.kde.StatusNotifierWatcher', 'RegisteredStatusNotifierItems'], text=True)
    for item in json.loads(response)['data']:
        name = item.split('/', 1)[0]
        result = subprocess.run(['busctl', '--user', '--json=short', 'call', 'org.freedesktop.DBus', '/org/freedesktop/DBus',
                                 'org.freedesktop.DBus', 'GetConnectionUnixProcessID', 's', name], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        if result.returncode == 0 and json.loads(result.stdout)['data'] == [pid]:
            return item
    return False


def main():
    case = Path(tempfile.mkdtemp(prefix='integrated-app-', dir=OUTPUT))
    home = case / 'home'
    home.mkdir()
    test_env = os.environ.copy()
    test_env.pop('ABDM_NATIVE_DESKTOP_ENVIRONMENT', None)
    test_env.update(HOME=str(home), XDG_CONFIG_HOME=str(home / '.config'),
                    XDG_DATA_HOME=str(home / '.local/share'), XDG_CACHE_HOME=str(home / '.cache'))
    profile = activation.paths(home)[1]
    bundle = (OUTPUT / 'latest').resolve()
    icon = ROOT / 'desktop/app/build/compose/binaries/main-release/app/ABDownloadManager/lib/ABDownloadManager.png'
    before = smoke.protected_snapshot()
    native_state = Path.home() / '.local/share/ab-download-manager-native-wayland/profile'
    if native_state.exists():
        # User's real native test profile is not a target of these checks.
        original_marker = (native_state / activation.MARKER).read_bytes() if (native_state / activation.MARKER).exists() else None
    else:
        original_marker = None
    with socket.socket() as reserve:
        reserve.bind(('127.0.0.1', 0))
        port = reserve.getsockname()[1]
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f'http://127.0.0.1:{server.server_port}{Handler.fixture_path}'
    result = {'case': str(case), 'errors': []}
    state = None
    try:
        state = activation.configure(bundle, icon, home, port=port, refresh=False, test_label='DISPOSABLE INTEGRATION TEST', environment=test_env)
        host = home / '.local/bin/ABDownloadManagerNativeMessagingHost'
        cli = home / '.local/bin/ABDownloadManagerCli'
        main_executable = home / '.local/bin/ABDownloadManager'
        result['ping'] = request_native(host, 'ping', None, env=test_env)
        assert not (profile / 'session-status.json').exists(), 'Protocol ping must not accidentally launch a GUI'
        # A browser add from a stopped app must awaken ONLY the native profile and show real UI.
        add = {'items': [{'type': 'http', 'link': url + '?dialog', 'suggestedName': 'browser-dialog.bin'}]}
        result['cold_add'] = request_native(host, 'add', add, env=test_env)
        def ready():
            record = json.loads((profile / 'session-status.json').read_text())
            return record if record.get('stage') == 'ready' and record.get('drawCallbacks', 0) > 0 else False
        record = await_condition(ready, 'cold native launch and browser URL dialog')
        pid = record['pid']
        assert record['toolkit'] == 'sun.awt.wl.WLToolkit' and record['desktopIntegration'] and record['tray']
        assert record['integrationPort'] == port and record['updates'] is False
        assert record['dataDir'] == str(profile / 'app-data')
        assert str(home / '.local/opt/ab-download-manager-native-wayland/releases') in record['uiSource']
        env = dict(e.split('=', 1) for e in Path(f'/proc/{pid}/environ').read_text().split('\0') if '=' in e)
        assert 'DISPLAY' not in env and env['HOME'] == str(profile / 'home')
        desktop_context = json.loads(env['ABDM_NATIVE_DESKTOP_ENVIRONMENT'])
        assert desktop_context['HOME'] == str(home) and desktop_context['XDG_DATA_HOME'] == str(home / '.local/share')
        maps = Path(f'/proc/{pid}/maps').read_text()
        assert 'libawt_wlawt.so' in maps and 'libawt_xawt.so' not in maps
        result['tray_item'] = await_condition(lambda: tray_owner_present(pid), 'native PID registered a real tray item')
        await_condition(lambda: smoke.listening_ports(pid) == {port, record['ipcPort']}, 'native IPC and HTTP listeners')
        for manifest in activation.manifest_paths(home):
            data = json.loads(manifest.read_text())
            assert data['name'] == 'com.abdownloadmanager'
            assert data['path'] == str(home / '.local/bin/ABDownloadManagerNativeWaylandHost')
        autostart = home / '.config/autostart/com.abdownloadmanager.desktop'
        await_condition(lambda: autostart.exists() and 'Native Wayland EXPERIMENT' in autostart.read_text(), 'real native autostart manager')
        assert '--background' in autostart.read_text() and 'ABDownloadManagerNativeWayland' in autostart.read_text()
        def api(path, data=None):
            body = json.dumps(data).encode() if data is not None else b''
            return urlopen(Request(f'http://127.0.0.1:{port}{path}', data=body, headers={'Content-Type': 'application/json'}), timeout=5).read()
        assert api('/ping') == b'pong'
        target = home / 'Downloads/ABDM-Native-Wayland'
        api('/start-headless-download', {'downloadSource': {'type': 'http', 'link': url + '?http'},
                                         'folder': str(target), 'name': 'http-engine.bin', 'startDownload': True})
        result['silent_add'] = request_native(host, 'add', {
            'items': [{'type': 'http', 'link': url + '?host', 'suggestedName': 'native-host-engine.bin'}],
            'options': {'silentAdd': True, 'silentStart': True}}, env=test_env)
        def downloaded():
            files = list(target.rglob('*.bin'))  # normal category rules may select Programs/, etc.
            valid = {file.name: file for file in files if hashlib.sha256(file.read_bytes()).hexdigest() == smoke.PAYLOAD_SHA256}
            expected = {'http-engine.bin', 'native-host-engine.bin'}
            return [valid[name] for name in sorted(expected)] if expected <= valid.keys() else False
        files = await_condition(downloaded, 'HTTP and native-host real-engine download checksums')
        result['downloads'] = {str(file.relative_to(target)): smoke.PAYLOAD_SHA256 for file in files}
        result['status'] = json.loads((profile / 'session-status.json').read_text())
        port_output = subprocess.check_output([str(cli), 'gui', 'integration', 'show'], text=True, timeout=15, env=test_env)
        assert str(port) in port_output
        second = subprocess.run([str(main_executable)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=20, env=test_env)
        assert second.returncode == 0 and 'instance already running' in second.stdout
        subprocess.run([str(main_executable), '--quit'], check=True, timeout=20, env=test_env)
        await_condition(lambda: not (profile / 'app-data/config/app.port').exists(), 'native graceful quit')
        # Background autostart route must also work, with persisted history and no X11.
        process = subprocess.Popen([str(main_executable), '--background'], stdout=subprocess.DEVNULL,
                                   stderr=(case / 'background.log').open('w'), start_new_session=True, env=test_env)
        reopened = await_condition(lambda: (r if (r := json.loads((profile / 'session-status.json').read_text())).get('pid') == process.pid
                                             and r.get('stage') == 'ready' else False), 'background native relaunch')
        assert reopened['loadedDownloads'] >= 2 and reopened['tray']
        result['background_reopen'] = reopened
        subprocess.run([str(main_executable), '--quit'], check=True, timeout=20, env=test_env)
        assert process.wait(timeout=15) == 0
        background_log = (case / 'background.log').read_text(errors='replace')
        assert 'Exception in thread' not in background_log and 'Swing hierarchy should be accessed only' not in background_log
        activation.finalize(Path(state['backup']))
        # Exercise complete rollback in this fake home, without touching the user's KDE cache.
        activation.installer.refresh_desktop = lambda _: None
        activation.restore(Path(state['backup']), environment=test_env)
        assert not (profile / activation.MARKER).exists()
        assert all(file.exists() for file in files), 'Rollback must preserve downloaded files'
        result['rollback'] = 'passed; downloads retained'
    except Exception as error:
        result['errors'].append(repr(error))
        raise
    finally:
        candidate = home / '.local/bin/ABDownloadManagerNativeWayland'
        if (profile / 'app-data/config/app.port').exists() and candidate.exists():
            subprocess.run([str(candidate), '--quit'], timeout=20, check=False, env=test_env)
        server.shutdown()
        server.server_close()
        if smoke.protected_snapshot() != before:
            result['errors'].append('real protected user files changed during disposable test')
        current_marker = (native_state / activation.MARKER).read_bytes() if (native_state / activation.MARKER).exists() else None
        assert current_marker == original_marker, 'Real native profile activation changed during disposable test'
        (OUTPUT / 'desktop-integration-results.json').write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps(result, indent=2))
    if result['errors']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
