#!/usr/bin/env python3
"""Launcher copied into the self-contained experimental bundle. Never starts installed ABDM."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
from urllib.parse import urlsplit

MARKER = '.abdm-experimental-profile'
MARKER_CONTENT = 'ABDM isolated desktop experiment v1\n'
DESKTOP_CONTEXT = 'ABDM_NATIVE_DESKTOP_ENVIRONMENT'
DESKTOP_PROPERTY = 'abdm.experimental.desktopEnvironment.'
DESKTOP_VARIABLES = ('HOME', 'XDG_CONFIG_HOME', 'XDG_DATA_HOME', 'XDG_CACHE_HOME', 'XDG_STATE_HOME',
                     'TMPDIR', 'DISPLAY', 'SKIKO_RENDER_API', 'WAYLAND_DEBUG')


def desktop_environment(env):
    # Keep the original context across native-host -> CLI -> detached GUI cold starts.
    # Otherwise a second wrapper invocation would mistake the isolated HOME for the desktop.
    raw = env.get(DESKTOP_CONTEXT)
    snapshot = json.loads(raw) if raw is not None else {name: env.get(name) for name in DESKTOP_VARIABLES}
    if (not isinstance(snapshot, dict) or set(snapshot) != set(DESKTOP_VARIABLES) or
            any(value is not None and (not isinstance(value, str) or '\0' in value) for value in snapshot.values())):
        raise ValueError('Invalid native desktop environment context')
    return snapshot


def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def verify(bundle):
    manifest = json.loads((bundle / 'bundle-manifest.json').read_text())
    if manifest.get('kind') != 'abdm-native-wayland-experiment-v1':
        raise ValueError('Not an owned experimental bundle')
    actual = {str(p.relative_to(bundle)) for p in bundle.rglob('*') if p.is_file() or p.is_symlink()}
    expected_files = set(manifest['files']) | set(manifest['symlinks']) | {'bundle-manifest.json'}
    if actual != expected_files:
        raise ValueError('Unexpected or missing files in the experimental bundle')
    for name, expected in manifest['files'].items():
        path = bundle / name
        if not path.resolve().is_relative_to(bundle.resolve()) or digest(path) != expected:
            raise ValueError(f'Changed/missing bundle artifact: {name}; rebuild, do not patch in place')
    for name, target in manifest['symlinks'].items():
        path = bundle / name
        if not path.is_symlink() or os.readlink(path) != target or not path.resolve().is_relative_to(bundle.resolve()):
            raise ValueError(f'Changed/escaping runtime symlink: {name}')
    return manifest


def prepare_profile(path):
    path = Path(os.path.abspath(path))
    if path != path.resolve():
        raise ValueError('Profile must not use symlink aliases')
    if path.exists() and any(path.iterdir()) and not (path / MARKER).is_file():
        raise ValueError('Refusing nonempty unowned profile directory')
    path.mkdir(parents=True, exist_ok=True, mode=0o700)
    if path.stat().st_uid != os.getuid():
        raise ValueError('Profile is not owned by this user')
    marker = path / MARKER
    if marker.is_symlink() or (marker.exists() and marker.read_text() != MARKER_CONTENT):
        raise ValueError('Unrecognized profile ownership marker')
    if not marker.exists():
        marker.write_text(MARKER_CONTENT)
    for child in ['home', 'config', 'data', 'cache', 'tmp', 'app-data']:
        directory = path / child
        if directory.exists() and directory.resolve() != directory:
            raise ValueError(f'Symlinked profile directory: {directory}')
        directory.mkdir(exist_ok=True, mode=0o700)
    (path / 'home/Downloads').mkdir(exist_ok=True)
    # Never follow an imported/symlinked config into a production data store.
    if any(p.is_symlink() for p in (path / 'app-data').rglob('*')):
        raise ValueError('Symlinks are not supported inside experimental application data')
    config = path / 'app-data/config'
    config.mkdir(exist_ok=True)
    settings = config / 'appSettings.json'
    if not settings.exists():
        settings.write_text(json.dumps({
            'apiEnabled': False, 'apiPort': 0, 'autoStartOnBoot': False, 'useSystemTray': False,
            'notificationSound': False, 'defaultDownloadFolder': str(path / 'home/Downloads'),
        }, indent=2) + '\n')
    return path


def command(bundle, profile, backend, smoke_test, env, download_url=None, download_sha256=None, quit_app=False,
            cli_args=None, native_args=None, background=False):
    if smoke_test and (profile / '.abdm-desktop-integration.properties').exists():
        raise ValueError('Smoke actions require an integration-disabled disposable profile')
    env = env.copy()
    desktop = desktop_environment(env)
    env[DESKTOP_CONTEXT] = json.dumps(desktop)
    for name in ['JAVA_TOOL_OPTIONS', 'JDK_JAVA_OPTIONS', '_JAVA_OPTIONS', 'CLASSPATH', 'LD_PRELOAD',
                 'LD_LIBRARY_PATH', 'JAVA_HOME', 'JDK_HOME', 'J2D_UISCALE', 'WAYLAND_DEBUG']:
        env.pop(name, None)
    env.update(HOME=str(profile / 'home'), XDG_CONFIG_HOME=str(profile / 'config'),
               XDG_DATA_HOME=str(profile / 'data'), XDG_CACHE_HOME=str(profile / 'cache'),
               TMPDIR=str(profile / 'tmp'), SKIKO_RENDER_API='SOFTWARE')
    non_window = quit_app or cli_args is not None or native_args is not None
    if sum([quit_app, cli_args is not None, native_args is not None]) > 1 or (non_window and (smoke_test or background or download_url)):
        raise ValueError('CLI/native-host/quit actions cannot be combined with GUI or smoke actions')
    if backend == 'wayland':
        if not env.get('WAYLAND_DISPLAY') and not non_window:
            raise ValueError('Native mode requires WAYLAND_DISPLAY; no implicit XWayland fallback')
        env.pop('DISPLAY', None)
        if smoke_test:
            env['WAYLAND_DEBUG'] = '1'
    elif not env.get('DISPLAY') and not non_window:
        raise ValueError('The explicit XWayland control requires DISPLAY')
    app = bundle / 'lib/app'
    # Use an explicit classpath with exactly one (patched) UI jar; no launcher manifest indirection.
    jars = sorted(app.glob('*.jar'))
    if any(p.name == 'pathing-classpath.jar' for p in jars):
        raise ValueError('Unsafe manifest classpath in experimental bundle')
    cmd = [str(bundle / 'runtime/bin/java'), '--enable-native-access=ALL-UNNAMED',
           f'-Dawt.toolkit.name={"WLToolkit" if backend == "wayland" else "XToolkit"}',
           f'-Dabdm.experimental.nativeWayland={str(backend == "wayland").lower()}',
           f'-Dabdm.experimental.backend={backend}', f'-Dabdm.experimental.profileRoot={profile}',
           f'-Dabdm.experimental.smokeTest={str(smoke_test).lower()}',
           f'-Duser.home={profile / "home"}', f'-Djava.io.tmpdir={profile / "tmp"}',
           '-Dabdm.systemUiScale=auto', f'-Dskiko.library.path={app}',
           f'-Dcompose.application.resources.dir={app / "resources"}',
           '-Dcompose.application.configure.swing.globals=true',
           # Deliberately no jpackage.app-path: no route to any installed wrapper/CLI/updater.
           '-cp', os.pathsep.join(map(str, jars)),
           'com.abdownloadmanager.desktop.experimental.NativeWaylandAppKt']
    # Null/unset is distinct from an empty value: the child must not keep profile overrides.
    cmd[1:1] = [f'-D{DESKTOP_PROPERTY}enabled=true'] + [
        f'-D{DESKTOP_PROPERTY}{name}={value}' for name, value in desktop.items() if value is not None]
    if quit_app:
        if smoke_test or download_url is not None:
            raise ValueError('--quit cannot be combined with smoke actions')
        cmd[-1:] = ['com.abdownloadmanager.desktop.cli.CliAppKt', 'gui', 'exit']
    if cli_args is not None:
        cmd[-1:] = ['com.abdownloadmanager.desktop.cli.CliAppKt', *cli_args]
    if native_args is not None:
        cmd[-1:] = ['com.abdownloadmanager.desktop.nativemessaging.host.NativeMessagingHostKt', *native_args]
    if background:
        cmd.append('--background')
    if download_url is not None:
        parsed = urlsplit(download_url)
        if (not smoke_test or parsed.scheme != 'http' or parsed.hostname != '127.0.0.1' or
                not parsed.path.startswith('/abdm-native-smoke/') or parsed.username or parsed.password or
                parsed.query or parsed.fragment or not parsed.port or
                not re.fullmatch('[a-f0-9]{64}', download_sha256 or '')):
            raise ValueError('Download smoke requires an explicit loopback fixture URL, checksum and --smoke-test')
        cmd[1:1] = [f'-Dabdm.experimental.smokeDownloadUrl={download_url}',
                    f'-Dabdm.experimental.smokeDownloadSha256={download_sha256}']
    return cmd, env


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backend', choices=['wayland', 'x11'], default='wayland')
    parser.add_argument('--profile', type=Path)
    parser.add_argument('--smoke-test', action='store_true', help='Bounded internal UI/lifecycle test, then graceful app exit')
    parser.add_argument('--verify-only', action='store_true')
    parser.add_argument('--quit', action='store_true', help='Gracefully quit ONLY the selected experimental profile via its own IPC')
    parser.add_argument('--background', action='store_true', help='Start in the tray (when enabled)')
    parser.add_argument('--cli', nargs=argparse.REMAINDER, help='Run the bundled CLI against this profile')
    parser.add_argument('--native-host', nargs=argparse.REMAINDER, help='Browser native messaging; stdout is protocol-only')
    parser.add_argument('--smoke-download-url', help=argparse.SUPPRESS)
    parser.add_argument('--smoke-download-sha256', help=argparse.SUPPRESS)
    args = parser.parse_args()
    bundle = Path(__file__).resolve().parents[1]
    try:
        manifest = verify(bundle)
        if args.verify_only:
            print(json.dumps({k: v for k, v in manifest.items() if k not in ['files', 'symlinks']}, indent=2))
            return
        profile = prepare_profile(args.profile or bundle.parent / 'profiles' / args.backend)
        cmd, env = command(bundle, profile, args.backend, args.smoke_test, os.environ,
                           args.smoke_download_url, args.smoke_download_sha256, args.quit,
                           args.cli, args.native_host, args.background)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    if args.native_host is None and args.cli is None:
        print(f'ABDM {args.backend} EXPERIMENT — separate profile: {profile}', flush=True)
        if (profile / '.abdm-desktop-integration.properties').exists():
            print('Desktop integration active; upstream self-updating remains disabled.', flush=True)
        else:
            print('Browser integration, autostart, tray, self-update, keep-awake and power actions are disabled.', flush=True)
    os.chdir(profile)
    os.execve(cmd[0], cmd, env)


if __name__ == '__main__':
    main()
