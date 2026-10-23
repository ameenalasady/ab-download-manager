#!/usr/bin/env python3
"""Install/remove an ABDM-only fractional-size fix for KWin's stock Stretch effect.

The app's native restore-on-drag starts the effect correctly, but KWin compares logical
sizes with exact equality and cancels on roundoff such as 1120.8000000000002 -> 1120.8.
Generate a user-local copy from the installed effect, changing only those comparisons
for ABDM. Never overwrite an unrelated override or change other applications' behavior.
Re-run install after KWin updates to refresh the copy; remove restores the stock effect.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

MARKER = '.abdm-maximize-workaround.json'
PATCH_ID = 'abdm-fractional-maximize-v1'
CLASS = 'com-abdownloadmanager-desktop-AppKt com-abdownloadmanager-desktop-AppKt'
FILES = {MARKER, 'metadata.json', 'contents/code/main.js'}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def build_script(source):
    replacements = {
        'class MaximizeEffect {': '''// ABDM-only workaround: movement must not look like a resize due to roundoff.
function abdmSameMaximizeSize(window, a, b) {
    if (window.windowClass == "''' + CLASS + '''") {
        return Math.abs(a.width - b.width) < 1e-6 && Math.abs(a.height - b.height) < 1e-6;
    }
    return a.width == b.width && a.height == b.height;
}

class MaximizeEffect {''',
        '(window.geometry.width == oldGeometry.width && window.geometry.height == oldGeometry.height)':
            'abdmSameMaximizeSize(window, window.geometry, oldGeometry)',
        '(window.oldGeometry.width == oldGeometry.width && window.oldGeometry.height == oldGeometry.height)':
            'abdmSameMaximizeSize(window, window.oldGeometry, oldGeometry)',
    }
    for old, new in replacements.items():
        if source.count(old) != 1:
            raise ValueError('Stock maximize effect changed; refusing to patch an unknown version. '
                             'Remove the existing workaround and review the new upstream code.')
        source = source.replace(old, new)
    return source


def check_owned(target):
    if target.is_symlink():
        raise ValueError(f'Refusing symlink override: {target}')
    if not target.exists():
        return
    marker = target / MARKER
    if not marker.is_file():
        raise ValueError(f'Existing override is not ours: {target}')
    info = json.loads(marker.read_text())
    if info.get('patchId') != PATCH_ID:
        raise ValueError(f'Unrecognized override marker: {target}')
    files = {p.relative_to(target).as_posix() for p in target.rglob('*') if p.is_file()}
    if files != FILES or any(p.is_symlink() for p in target.rglob('*')):
        raise ValueError(f'Override has extra files or symlinks; leaving it untouched: {target}')
    for name, key in [('contents/code/main.js', 'generatedSha256'), ('metadata.json', 'metadataSha256')]:
        if digest((target / name).read_bytes()) != info.get(key):
            raise ValueError(f'Override was edited; leaving it untouched: {target / name}')


def install(source_dir, target):
    check_owned(target)
    original = (source_dir / 'contents/code/main.js').read_bytes()
    metadata = (source_dir / 'metadata.json').read_bytes()
    generated = build_script(original.decode()).encode()
    if json.loads(metadata)['KPlugin']['Id'] != 'maximize':
        raise ValueError('Source is not the maximize effect')
    (target / 'contents/code').mkdir(parents=True, exist_ok=True)
    (target / 'contents/code/main.js').write_bytes(generated)
    (target / 'metadata.json').write_bytes(metadata)
    (target / MARKER).write_text(json.dumps({
        'patchId': PATCH_ID,
        'source': str(source_dir),
        'sourceSha256': digest(original),
        'generatedSha256': digest(generated),
        'metadataSha256': digest(metadata),
    }, indent=2) + '\n')


def remove(target):
    check_owned(target)
    if target.exists():
        shutil.rmtree(target)


def reload_effect():
    prefix = ['qdbus', 'org.kde.KWin', '/Effects']
    # Do not enable an effect that the user has deliberately disabled.
    def call(method):
        return subprocess.run(prefix + ['org.kde.kwin.Effects.' + method, 'maximize'],
                              check=True, capture_output=True, text=True, timeout=10).stdout.strip()
    if call('isEffectLoaded') == 'true':
        call('unloadEffect')
        call('loadEffect')
        if call('isEffectLoaded') != 'true':
            raise RuntimeError('KWin did not reload maximize; remove the workaround to restore stock')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['install', 'remove'])
    parser.add_argument('--source', type=Path, default=Path('/usr/share/kwin-wayland/effects/maximize'))
    parser.add_argument('--data-home', type=Path,
                        default=Path(os.environ.get('XDG_DATA_HOME') or Path.home() / '.local/share'))
    parser.add_argument('--no-reload', action='store_true', help='Apply on disk only (e.g. outside a Plasma session)')
    args = parser.parse_args()
    target = args.data_home / 'kwin-wayland/effects/maximize'
    try:
        if args.action == 'install':
            install(args.source, target)
        else:
            remove(target)
        if not args.no_reload:
            reload_effect()
    except (ValueError, OSError, subprocess.SubprocessError, RuntimeError) as error:
        parser.exit(1, f'{error}\n')
    print(f'{args.action}: {target}')


if __name__ == '__main__':
    main()
