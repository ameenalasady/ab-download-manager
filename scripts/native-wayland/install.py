#!/usr/bin/env python3
"""Explicit user-local native desktop activation. Never migrates data or edits fallback integration."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]
KIND = 'abdm-native-desktop-install-v1'


def load_launcher():
    spec = importlib.util.spec_from_file_location('abdm_native_install_launcher', Path(__file__).with_name('launch.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sha(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def atomic_write(path, content, mode=0o644):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.' + path.name + '-', dir=path.parent)
    try:
        with os.fdopen(fd, 'w') as target:
            target.write(content)
        os.chmod(temporary, mode)
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)


def desktop_quote(path):
    value = str(path)
    if any(char in value for char in '\n\r\x00'):
        raise ValueError('Invalid desktop path')
    for char in ['\\', '"', '`', '$']:
        value = value.replace(char, '\\' + char)
    # General desktop-value escaping is applied before Exec argument unquoting.
    return '"' + value.replace('\\', '\\\\').replace('%', '%%') + '"'


def refresh_desktop(home):
    applications = home / '.local/share/applications'
    for command in [['update-desktop-database', str(applications)], ['kbuildsycoca6', '--noincremental']]:
        if shutil.which(command[0]):
            subprocess.run(command, check=True)


def install(bundle, icon, home, refresh=True):
    launcher = load_launcher()
    bundle, home = bundle.resolve(), home.resolve()
    metadata = launcher.verify(bundle)
    if not icon.is_file() or not (bundle / 'bin/ABDownloadManagerNativeWayland').is_file() or not (bundle / 'runtime/bin/java').is_file():
        raise ValueError('A complete native bundle and real application PNG icon are required')
    base = home / '.local/opt/ab-download-manager-native-wayland'
    profile = home / '.local/share/ab-download-manager-native-wayland/profile'
    wrapper = home / '.local/bin/ABDownloadManagerNativeWayland'
    desktop = home / '.local/share/applications/com.abdownloadmanager.desktop'
    record_path = base / 'installation.json'
    if base.exists() and (base.is_symlink() or (any(base.iterdir()) and not record_path.is_file())):
        raise ValueError('Refusing unrelated native installation directory')
    old = json.loads(record_path.read_text()) if record_path.exists() else None
    if old and old.get('kind') != KIND:
        raise ValueError('Unknown native installation owner')
    if desktop.is_symlink() or wrapper.is_symlink():
        raise ValueError('Refusing symlinked desktop entry or native wrapper')
    if wrapper.exists() and (not old or sha(wrapper) != old.get('wrapper_sha256')):
        raise ValueError('Refusing unrelated or edited native wrapper')
    current = base / 'current'
    if current.exists() or current.is_symlink():
        if not old or not current.is_symlink() or current.resolve() != Path(old['release']):
            raise ValueError('Refusing an unrelated native current link')
    # Reject aliases before writing an application, profile or launcher through them.
    for path in [base, profile, wrapper.parent, desktop.parent]:
        if path != path.resolve():
            raise ValueError(f'Symlinked installation path: {path}')
    release = base / 'releases' / bundle.name
    if release.exists():
        launcher.verify(release)
        if sha(release / 'bundle-manifest.json') != sha(bundle / 'bundle-manifest.json'):
            raise ValueError('Existing release name has different contents')
    backup = home / '.local/state/abdm-backups' / ('native-desktop-' + time.strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:6])
    backup.mkdir(parents=True)
    previous = desktop.exists()
    if previous:
        shutil.copy2(desktop, backup / 'previous.desktop')
    if old:
        shutil.copy2(record_path, backup / 'previous-installation.json')
    if not release.exists():
        release.parent.mkdir(parents=True, exist_ok=True)
        staging = release.with_name('.' + release.name + '-' + uuid.uuid4().hex)
        shutil.copytree(bundle, staging, symlinks=True)
        launcher.verify(staging)
        staging.rename(release)
    # This is a new native profile only; existing native preferences/history are retained.
    fresh = not (profile / launcher.MARKER).exists()
    launcher.prepare_profile(profile)
    if fresh:
        settings = profile / 'app-data/config/appSettings.json'
        data = json.loads(settings.read_text())
        downloads = home / 'Downloads/ABDM-Native-Wayland'
        downloads.mkdir(parents=True, exist_ok=True)
        data['defaultDownloadFolder'] = str(downloads)
        atomic_write(settings, json.dumps(data, indent=2) + '\n', 0o600)
    temporary_icon = base / ('.icon-' + uuid.uuid4().hex)
    shutil.copy2(icon, temporary_icon)
    os.replace(temporary_icon, base / 'ABDownloadManager.png')
    temporary_link = base / ('.current-' + uuid.uuid4().hex)
    temporary_link.symlink_to(release.relative_to(base), target_is_directory=True)
    os.replace(temporary_link, current)
    wrapper_text = ('#!/bin/sh\n# ABDM native desktop test; managed by scripts/native-wayland/install.py\n'
                    'exec ' + shlex.quote(str(current / 'bin/ABDownloadManagerNativeWayland')) +
                    ' --profile ' + shlex.quote(str(profile)) + ' "$@"\n')
    atomic_write(wrapper, wrapper_text, 0o755)
    desktop_text = ('[Desktop Entry]\nType=Application\nName=AB Download Manager (Native Wayland)\n'
                    'GenericName=Download Manager\nComment=Native Wayland experimental build; separate data profile\n'
                    'Categories=Network;FileTransfer;\nKeywords=download;downloader;manager;wayland;\n'
                    f'Exec={desktop_quote(wrapper)}\nIcon={base / "ABDownloadManager.png"}\n'
                    'Terminal=false\nStartupNotify=false\n'
                    'StartupWMClass=com.abdownloadmanager.desktop.experimental.NativeWaylandAppKt\n'
                    'X-ABDM-Native-Wayland=true\n')
    staged_desktop = backup / 'native.desktop'
    staged_desktop.write_text(desktop_text)
    if shutil.which('desktop-file-validate'):
        subprocess.run(['desktop-file-validate', str(staged_desktop)], check=True)
    new_sha = sha(staged_desktop)
    record = {'kind': KIND, 'release': str(release), 'version': metadata['version'],
              'profile': str(profile), 'desktop': str(desktop), 'desktop_sha256': new_sha,
              'wrapper_sha256': sha(wrapper), 'backup': str(backup), 'previous_desktop': previous,
              'previous_sha256': sha(backup / 'previous.desktop') if previous else None}
    atomic_write(backup / 'activation.json', json.dumps(record, indent=2) + '\n')
    rollback = '''#!/bin/sh
set -eu
python3 - "$0" <<'PY'
from pathlib import Path
import hashlib, json, os, shutil, sys, tempfile
backup = Path(sys.argv[1]).resolve().parent
record = json.loads((backup / 'activation.json').read_text())
target = Path(record['desktop'])
if target.is_symlink() or not target.is_file() or hashlib.sha256(target.read_bytes()).hexdigest() != record['desktop_sha256']:
    raise SystemExit('Desktop entry changed after activation; refusing to overwrite it. Inspect previous.desktop manually.')
if record['previous_desktop']:
    if hashlib.sha256((backup / 'previous.desktop').read_bytes()).hexdigest() != record['previous_sha256']:
        raise SystemExit('Previous desktop backup changed; refusing restoration.')
    fd, tmp = tempfile.mkstemp(prefix='.abdm-restore-', dir=target.parent)
    os.close(fd)
    shutil.copy2(backup / 'previous.desktop', tmp)
    os.replace(tmp, target)
else:
    target.unlink()
print('Previous desktop entry restored. Native app/data retained; no running downloads were stopped.')
PY
command -v update-desktop-database >/dev/null && update-desktop-database ''' + shlex.quote(str(desktop.parent)) + '''
if command -v kbuildsycoca6 >/dev/null; then kbuildsycoca6 --noincremental; fi
'''
    atomic_write(backup / 'restore-desktop.sh', rollback, 0o700)
    atomic_write(record_path, json.dumps(record, indent=2) + '\n')
    # Desktop activation is LAST; fallback binaries/wrappers/autostart/browser/KWin are untouched.
    atomic_write(desktop, desktop_text)
    if refresh:
        refresh_desktop(home)
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', type=Path, default=ROOT / 'build/native-wayland/latest')
    parser.add_argument('--icon', type=Path, default=ROOT / 'desktop/app/build/compose/binaries/main-release/app/ABDownloadManager/lib/ABDownloadManager.png')
    parser.add_argument('--activate-desktop', action='store_true', required=True,
                        help='Explicitly approve replacing your user-local ABDM desktop entry')
    args = parser.parse_args()
    result = install(args.bundle, args.icon, Path.home())
    print(json.dumps(result, indent=2))
    print('Rollback:', Path(result['backup']) / 'restore-desktop.sh')
    print('Old XWayland process, installation, integration and data were not stopped or changed.')


if __name__ == '__main__':
    main()
