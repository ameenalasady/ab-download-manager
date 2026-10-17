#!/usr/bin/env python3
"""Build-only, self-contained Linux x64 native Wayland experiment. NEVER installs/restarts ABDM."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/wayland-probe'))
import native_ui

OUTPUT = ROOT / 'build/native-wayland'
PROBE = ROOT / 'build/wayland-probe'
APP = ROOT / 'desktop/app/build/compose/binaries/main-release/app/ABDownloadManager/lib/app'
RUNTIME_ARCHIVE = PROBE / 'jbr-25.0.4.1-linux-x64-b635.72.tar.gz'
RUNTIME_SHA512 = 'b7cff4d94f5e8240024834d966451c15275b9d5ed01b8a806b0d4ef84248abb53b0b58beb40a8518ab1d2bba8d36878e04424e2251e3b0be87098fed219cc47d'


def run(*args):
    env = os.environ.copy()
    env.update(JAVA_HOME='/usr/lib/jvm/java-25-openjdk', PYTHONDONTWRITEBYTECODE='1')
    subprocess.run(args, cwd=ROOT, env=env, check=True)


def extract_runtime(archive, destination):
    with archive.open('rb') as source:
        if hashlib.file_digest(source, 'sha512').hexdigest() != RUNTIME_SHA512:
            raise ValueError('Unverified runtime archive; use the pinned download in the probe README')
    destination.mkdir(parents=True)
    with tarfile.open(archive) as source:
        roots = {Path(m.name).parts[0] for m in source.getmembers()}
        if len(roots) != 1:
            raise ValueError('Unexpected runtime archive layout')
        source.extractall(destination, filter='data')
    runtime = destination / roots.pop()
    for name in ['bin/java', 'lib/modules', 'lib/libawt_wlawt.so', 'release']:
        if not (runtime / name).is_file():
            raise ValueError(f'Incomplete Wayland-capable runtime: {name}')
    return runtime


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--skip-gradle', action='store_true', help='Reuse a current local distributable (developer only)')
    args = parser.parse_args()
    if not args.skip_gradle:
        run(str(ROOT / 'gradlew'), '-PlocalAppVersion=1.11.0+native.3', ':desktop:shared:test',
            ':desktop:app-utils:test', ':desktop:app:createReleaseDistributable')
    run(sys.executable, str(ROOT / 'scripts/wayland-probe/compile.py'), '--native-ui')
    original = list(APP.glob('ui-desktop-1.12.1-*.jar'))
    if len(original) != 1:
        parser.error('Expected exactly one pinned upstream UI jar')
    artifact = PROBE / 'ui-desktop-1.12.1-abdm-native-experimental.jar'
    ui_manifest = json.loads((PROBE / 'native-ui-manifest.json').read_text())
    native_ui.validate_artifact(original[0], artifact, ui_manifest)
    name = 'ABDownloadManagerNativeWayland-' + time.strftime('%Y%m%d-%H%M%S')
    bundle = OUTPUT / 'releases' / name
    bundle.mkdir(parents=True)  # never overwrite an existing bundle
    try:
        runtime = extract_runtime(RUNTIME_ARCHIVE, bundle / 'runtime-extraction')
        runtime.rename(bundle / 'runtime')
        (bundle / 'runtime-extraction').rmdir()
        excluded = {original[0].name, 'pathing-classpath.jar'}
        shutil.copytree(APP, bundle / 'lib/app', ignore=lambda directory, names: excluded if Path(directory) == APP else [])
        shutil.copy2(artifact, bundle / 'lib/app' / artifact.name)
        (bundle / 'bin').mkdir()
        launcher = bundle / 'bin/ABDownloadManagerNativeWayland'
        shutil.copy2(Path(__file__).with_name('launch.py'), launcher)
        launcher.chmod(0o755)
        shutil.copy2(ROOT / 'LICENSE', bundle / 'LICENSE')
        shutil.copy2(Path(__file__).with_name('README.md'), bundle / 'README.md')
        shutil.copy2(ROOT / 'scripts/wayland-probe/compose-1.12.1-native-wayland.patch', bundle / 'compose-native-wayland.patch')
        files, symlinks = {}, {}
        for path in sorted(bundle.rglob('*')):
            relative = str(path.relative_to(bundle))
            if path.is_symlink():
                if not path.resolve().is_relative_to(bundle.resolve()):
                    raise ValueError(f'Escaping runtime symlink: {relative}')
                symlinks[relative] = os.readlink(path)
            elif path.is_file():
                files[relative] = native_ui.sha256(path)
        manifest = {
            'kind': 'abdm-native-wayland-experiment-v1', 'version': '1.11.0+native.3',
            'runtime': '25.0.4.1+1-b635.72', 'runtime_archive_sha512': RUNTIME_SHA512,
            'runtime_strategy': 'complete pinned JBR runtime, not cross-vendor jlink',
            'desktop_integration': True,
            'ui': ui_manifest, 'renderer': 'SOFTWARE + explicit opt-in SwingGraphics on WLToolkit',
            'git_head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
            'git_dirty': bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT)),
            'files': files, 'symlinks': symlinks,
        }
        (bundle / 'bundle-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        run(str(launcher), '--verify-only')
        archive = OUTPUT / 'ABDownloadManager-native-wayland-experimental-linux-x64.tar.gz'
        with tarfile.open(archive.with_suffix('.tmp'), 'w:gz') as target:
            target.add(bundle, arcname=bundle.name)
        archive.with_suffix('.tmp').replace(archive)
        (archive.with_suffix(archive.suffix + '.sha256')).write_text(native_ui.sha256(archive) + '  ' + archive.name + '\n')
        latest = OUTPUT / 'latest'
        if latest.exists() and not latest.is_symlink():
            raise ValueError('Refusing unrelated non-symlink build/native-wayland/latest')
        latest.unlink(missing_ok=True)
        latest.symlink_to(bundle.relative_to(OUTPUT), target_is_directory=True)
        print('\nBUNDLE:', bundle)
        print('ARCHIVE:', archive)
        print('LAUNCH:', latest / 'bin/ABDownloadManagerNativeWayland')
        print('No installation, desktop-entry change, production data access, or app restart performed.')
    except BaseException:
        print(f'Incomplete experimental bundle retained for inspection: {bundle}', file=sys.stderr)
        raise


if __name__ == '__main__':
    main()
