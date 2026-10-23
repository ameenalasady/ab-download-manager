#!/usr/bin/env python3
"""Explicit native takeover of browser/CLI/autostart entrypoints, with profile-preserving rollback."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import time
import uuid

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('native_desktop_installer', HERE / 'install.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)
ROOT = installer.ROOT
MARKER = '.abdm-desktop-integration.properties'
KIND = 'abdm-native-desktop-integration-v1'
BROWSERS = ['google-chrome', 'chromium', 'BraveSoftware/Brave-Browser', 'microsoft-edge', 'vivaldi', 'opera']
PREFERENCES = ['apiEnabled', 'apiPort', 'useSystemTray', 'autoStartOnBoot']


def paths(home):
    base = home / '.local/opt/ab-download-manager-native-wayland'
    profile = home / '.local/share/ab-download-manager-native-wayland/profile'
    return base, profile


def manifest_paths(home):
    return [home / '.mozilla/native-messaging-hosts/com.abdownloadmanager.json', *[
        home / '.config' / browser / 'NativeMessagingHosts/com.abdownloadmanager.json' for browser in BROWSERS]]


def active_downloads(database):
    count = 0
    def count_items(value):
        nonlocal count
        if isinstance(value, dict):
            count += value.get('status') == 'Downloading'
            for child in value.values():
                count_items(child)
        elif isinstance(value, list):
            for child in value:
                count_items(child)
    for file in (database / 'downloadlist').rglob('*'):
        if file.is_file():
            count_items(json.loads(file.read_text()))
    return count


def stop_if_running(executable, config, *arguments, environment=None):
    if not (config / 'app.port').exists():
        return
    if not executable.is_file():
        raise ValueError(f'Cannot safely stop the instance for {config}')
    subprocess.run([str(executable), *arguments], check=True, timeout=25, env=environment)
    deadline = time.monotonic() + 20
    while (config / 'app.port').exists() and time.monotonic() < deadline:
        time.sleep(.1)
    if (config / 'app.port').exists():
        raise RuntimeError(f'Instance did not cleanly exit: {config}; refusing takeover')


def snapshot(path, backup, index):
    if path.is_symlink():
        return {'type': 'symlink', 'target': os.readlink(path)}
    if path.is_file():
        name = f'file-{index}'
        shutil.copy2(path, backup / name)
        return {'type': 'file', 'backup': name, 'sha256': installer.sha(path)}
    if path.exists():
        raise ValueError(f'Not a replaceable integration file: {path}')
    return {'type': 'absent'}


def configure(bundle, icon, home, port=15151, refresh=True, test_label=None, environment=None):
    home = home.resolve()
    base, profile = paths(home)
    metadata = installer.load_launcher().verify(bundle.resolve())
    if metadata.get('desktop_integration') is not True or not icon.is_file() or not 1 <= port <= 65535:
        raise ValueError('A verified integration-capable native bundle, icon and nonzero port are required')
    owner = base / 'desktop-integration.json'
    previous_activation = json.loads(owner.read_text()) if owner.exists() else None
    for name in ['ABDownloadManagerNativeWaylandCli', 'ABDownloadManagerNativeWaylandHost']:
        executable = home / '.local/bin' / name
        if executable.exists() or executable.is_symlink():
            if not previous_activation or previous_activation.get('kind') != KIND:
                raise ValueError(f'Refusing an unrelated native integration executable: {executable}')
            prior = json.loads((Path(previous_activation['backup']) / 'transition.json').read_text())
            expected = prior['applied'].get(str(executable), {})
            if executable.is_symlink() or installer.sha(executable) != expected.get('sha256'):
                raise ValueError(f'Native integration wrapper was edited: {executable}')
    # Do not interrupt a real transfer, and never silently import/rewrite the old database.
    for database in [home / '.abdm/config/download_db', profile / 'app-data/config/download_db']:
        if active_downloads(database):
            raise RuntimeError(f'Finish or pause active downloads first: {database}')
    stop_if_running(home / '.local/bin/ABDownloadManagerNativeWayland', profile / 'app-data/config', '--quit', environment=environment)
    stop_if_running(home / '.local/opt/ab-download-manager-plasma/current/bin/ABDownloadManagerCli',
                    home / '.abdm/config', 'gui', 'exit', environment=environment)
    backup = home / '.local/state/abdm-backups' / ('native-integration-' + time.strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:6])
    backup.mkdir(parents=True, mode=0o700)
    target_names = ['ABDownloadManager', 'ABDownloadManagerCli', 'ABDownloadManagerNativeMessagingHost',
                    'ABDownloadManagerNativeWayland', 'ABDownloadManagerNativeWaylandCli', 'ABDownloadManagerNativeWaylandHost']
    targets = [*[home / '.local/bin' / name for name in target_names], *manifest_paths(home),
               home / '.config/autostart/com.abdownloadmanager.desktop',
               home / '.local/share/applications/com.abdownloadmanager.desktop',
               profile / MARKER, base / 'current', base / 'installation.json', base / 'desktop-integration.json']
    previous = {str(path): snapshot(path, backup, index) for index, path in enumerate(targets)}
    settings = profile / 'app-data/config/appSettings.json'
    saved = json.loads(settings.read_text()) if settings.exists() else {}
    state = {'kind': KIND, 'home': str(home), 'profile': str(profile), 'backup': str(backup),
             'previous': previous, 'previous_preferences': {key: saved.get(key) for key in PREFERENCES}}
    installer.atomic_write(backup / 'transition.json', json.dumps(state, indent=2) + '\n')
    # Copy the restore implementation: rollback does not depend on the checkout surviving.
    for name in ['desktop_integration.py', 'install.py', 'launch.py']:
        shutil.copy2(HERE / name, backup / name)
    installer.atomic_write(backup / 'restore-integrations.sh',
                           '#!/bin/sh\nexport PYTHONDONTWRITEBYTECODE=1\nexec python3 ' +
                           shlex.quote(str(backup / 'desktop_integration.py')) + ' --restore ' + shlex.quote(str(backup)) + '\n', 0o700)
    record = installer.install(bundle, icon, home, refresh=False)
    main = home / '.local/bin/ABDownloadManagerNativeWayland'
    entry = base / 'current/bin/ABDownloadManagerNativeWayland'
    def wrapper(name, mode):
        content = '#!/bin/sh\n# ABDM native integration; managed, do not edit\nexec ' + shlex.quote(str(entry)) + \
                  ' --profile ' + shlex.quote(str(profile)) + ' ' + mode + ' "$@"\n'
        installer.atomic_write(home / '.local/bin' / name, content, 0o755)
    wrapper('ABDownloadManagerNativeWaylandCli', '--cli')
    wrapper('ABDownloadManagerNativeWaylandHost', '--native-host')
    for generic, dedicated in [('ABDownloadManager', 'ABDownloadManagerNativeWayland'),
                               ('ABDownloadManagerCli', 'ABDownloadManagerNativeWaylandCli'),
                               ('ABDownloadManagerNativeMessagingHost', 'ABDownloadManagerNativeWaylandHost')]:
        installer.atomic_write(home / '.local/bin' / generic,
                               '#!/bin/sh\n# ABDM native integration alias\nexec ' +
                               shlex.quote(str(home / '.local/bin' / dedicated)) + ' "$@"\n', 0o755)
    installer.atomic_write(profile / MARKER,
                           f'kind={KIND}\nhome=' + str(home).replace('\\', '\\\\') + '\n' +
                           (f'testLabel={test_label}\n' if test_label else ''), 0o600)
    data = json.loads(settings.read_text())
    if previous_activation is None:
        data.update(apiEnabled=True, apiPort=port, useSystemTray=True, autoStartOnBoot=True)
    installer.atomic_write(settings, json.dumps(data, indent=2) + '\n', 0o600)
    # Replace the old autostart with a marked native entry before application startup.
    installer.atomic_write(home / '.config/autostart/com.abdownloadmanager.desktop',
                           '[Desktop Entry]\nType=Application\nName=AB Download Manager (Native Wayland)\n'
                           f'Exec={installer.desktop_quote(main)} --background\nTerminal=false\nNoDisplay=true\n'
                           'StartupNotify=false\nX-GNOME-Autostart-enabled=true\nX-KDE-autostart-phase=2\nX-ABDM-Native-Wayland=true\n')
    subprocess.run([str(home / '.local/bin/ABDownloadManagerNativeWaylandCli'), 'native-messaging', 'install'],
                   check=True, timeout=25, env=environment)
    state['installation'] = record
    installer.atomic_write(backup / 'transition.json', json.dumps(state, indent=2) + '\n')
    finalize(backup)
    if refresh:
        installer.refresh_desktop(home)
    return state


def finalize(backup):
    state = json.loads((backup / 'transition.json').read_text())
    state['applied'] = {name: snapshot(Path(name), backup, 'applied-' + str(index))
                        for index, name in enumerate(state['previous'])}
    installer.atomic_write(backup / 'transition.json', json.dumps(state, indent=2) + '\n')
    base, _ = paths(Path(state['home']))
    installer.atomic_write(base / 'desktop-integration.json', json.dumps({'kind': KIND, 'backup': str(backup)}, indent=2) + '\n')


def restore(backup, environment=None):
    state = json.loads((backup / 'transition.json').read_text())
    if state.get('kind') != KIND:
        raise ValueError('Unknown integration backup')
    home, profile = Path(state['home']), Path(state['profile'])
    if active_downloads(profile / 'app-data/config/download_db'):
        raise RuntimeError('Finish or pause native downloads before restoring integrations')
    # Verify managed artifacts before stopping an app or restoring anything.
    for name, expected in state.get('applied', {}).items():
        path = Path(name)
        if name.endswith('/desktop-integration.json'):
            continue  # owner/backup pointer is written after the final applied snapshot
        actual = snapshot(path, backup, 'check')
        if expected['type'] == 'file':
            if actual['type'] != 'file' or actual['sha256'] != expected['sha256']:
                raise ValueError(f'Integration changed after activation; refusing overwrite: {name}')
        elif actual != expected:
            raise ValueError(f'Integration changed after activation: {name}')
    for old in state['previous'].values():
        if old['type'] == 'file' and installer.sha(backup / old['backup']) != old['sha256']:
            raise ValueError('A rollback source was changed; refusing partial restoration')
    stop_if_running(home / '.local/bin/ABDownloadManagerNativeWayland', profile / 'app-data/config', '--quit', environment=environment)
    settings = profile / 'app-data/config/appSettings.json'
    data = json.loads(settings.read_text())
    for key, value in state['previous_preferences'].items():
        if value is None:
            data.pop(key, None)
        else:
            data[key] = value
    installer.atomic_write(settings, json.dumps(data, indent=2) + '\n', 0o600)
    for name, old in state['previous'].items():
        path = Path(name)
        if old['type'] == 'file':
            source = backup / old['backup']
            if installer.sha(source) != old['sha256']:
                raise ValueError(f'Changed rollback source: {source}')
            fd, temporary = tempfile_file(path)
            os.close(fd)
            shutil.copy2(source, temporary)
            os.replace(temporary, path)
        elif old['type'] == 'symlink':
            temporary = path.with_name('.restore-' + uuid.uuid4().hex)
            temporary.symlink_to(old['target'])
            os.replace(temporary, path)
        else:
            path.unlink(missing_ok=True)
    installer.refresh_desktop(home)
    print('Previous browser/CLI/autostart/desktop integration restored; all native history/downloads retained.')


def tempfile_file(path):
    import tempfile
    path.parent.mkdir(parents=True, exist_ok=True)
    return tempfile.mkstemp(prefix='.abdm-restore-', dir=path.parent)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--activate', action='store_true')
    group.add_argument('--restore', type=Path)
    group.add_argument('--finalize', type=Path)
    parser.add_argument('--bundle', type=Path, default=ROOT / 'build/native-wayland/latest')
    parser.add_argument('--port', type=int, default=15151)
    args = parser.parse_args()
    if args.restore:
        restore(args.restore)
    elif args.finalize:
        finalize(args.finalize)
    else:
        icon = ROOT / 'desktop/app/build/compose/binaries/main-release/app/ABDownloadManager/lib/ABDownloadManager.png'
        print(json.dumps(configure(args.bundle, icon, Path.home(), args.port), indent=2))


if __name__ == '__main__':
    main()
