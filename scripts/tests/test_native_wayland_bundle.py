import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SCRIPTS = Path(__file__).resolve().parents[1] / 'native-wayland'


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


launcher = load('native_wayland_launcher', 'launch.py')
builder = load('native_wayland_builder', 'build.py')


class NativeBundleTest(unittest.TestCase):
    def test_profile_is_owned_separate_and_seeded_only_once(self):
        with tempfile.TemporaryDirectory() as folder:
            profile = launcher.prepare_profile(Path(folder) / 'profile')
            settings = profile / 'app-data/config/appSettings.json'
            data = json.loads(settings.read_text())
            self.assertFalse(data['apiEnabled'])
            self.assertFalse(data['useSystemTray'])
            self.assertFalse(data['autoStartOnBoot'])
            self.assertEqual(data['apiPort'], 0)
            self.assertEqual(data['defaultDownloadFolder'], str(profile / 'home/Downloads'))
            settings.write_text('{"ownedPreference": true}')
            launcher.prepare_profile(profile)
            self.assertEqual(json.loads(settings.read_text()), {'ownedPreference': True})

    def test_unowned_profiles_and_symlinked_paths_are_not_adopted(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'production.json').write_text('untouched')
            with self.assertRaises(ValueError):
                launcher.prepare_profile(root)
            self.assertEqual((root / 'production.json').read_text(), 'untouched')
            profile = launcher.prepare_profile(root / 'owned')
            alias = root / 'alias'
            alias.symlink_to(profile, target_is_directory=True)
            with self.assertRaises(ValueError):
                launcher.prepare_profile(alias)
            (profile / 'app-data/config/symlink').symlink_to(root / 'production.json')
            with self.assertRaises(ValueError):
                launcher.prepare_profile(profile)
            self.assertEqual((root / 'production.json').read_text(), 'untouched')

    def test_foreign_marker_is_not_rewritten(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            marker = root / launcher.MARKER
            marker.write_text('another application')
            with self.assertRaises(ValueError):
                launcher.prepare_profile(root)
            self.assertEqual(marker.read_text(), 'another application')

    def test_native_launch_is_explicit_and_removes_x11_and_injected_options(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'lib/app').mkdir(parents=True)
            (root / 'lib/app/patched-ui.jar').touch()
            original = {'DISPLAY': ':0', 'WAYLAND_DISPLAY': 'wayland-0', 'JAVA_TOOL_OPTIONS': '-Dunsafe=true',
                        'LD_PRELOAD': '/injected.so', 'HOME': '/normal/home'}
            cmd, env = launcher.command(root, root / 'profile', 'wayland', True, original)
            self.assertNotIn('DISPLAY', env)
            self.assertNotIn('JAVA_TOOL_OPTIONS', env)
            self.assertNotIn('LD_PRELOAD', env)
            self.assertEqual(original['DISPLAY'], ':0')
            self.assertEqual(env['HOME'], str(root / 'profile/home'))
            self.assertIn('-Dawt.toolkit.name=WLToolkit', cmd)
            self.assertIn('-Dabdm.experimental.nativeWayland=true', cmd)
            self.assertEqual(cmd[-1], 'com.abdownloadmanager.desktop.experimental.NativeWaylandAppKt')
            self.assertFalse(any('jpackage.app-path' in arg for arg in cmd))
            with self.assertRaises(ValueError):
                launcher.command(root, root / 'profile', 'wayland', False, {'DISPLAY': ':0'})

    def test_desktop_context_survives_internal_cold_start_but_app_stays_isolated(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            original = {'HOME': '/desktop/home', 'XDG_DATA_HOME': '/desktop/custom-data',
                        'XDG_CACHE_HOME': '', 'DISPLAY': ':9', 'WAYLAND_DISPLAY': 'wayland-0',
                        'JAVA_TOOL_OPTIONS': '-Dunsafe=true'}
            first, isolated = launcher.command(root, root / 'profile', 'wayland', False, original, native_args=[])
            second, nested = launcher.command(root, root / 'profile', 'wayland', False, isolated)
            for cmd, env in [(first, isolated), (second, nested)]:
                self.assertIn('-D' + launcher.DESKTOP_PROPERTY + 'HOME=/desktop/home', cmd)
                self.assertIn('-D' + launcher.DESKTOP_PROPERTY + 'XDG_DATA_HOME=/desktop/custom-data', cmd)
                self.assertIn('-D' + launcher.DESKTOP_PROPERTY + 'XDG_CACHE_HOME=', cmd)
                self.assertIn('-D' + launcher.DESKTOP_PROPERTY + 'DISPLAY=:9', cmd)
                self.assertFalse(any(arg.startswith('-D' + launcher.DESKTOP_PROPERTY + 'XDG_CONFIG_HOME=') for arg in cmd))
                self.assertEqual(env['HOME'], str(root / 'profile/home'))
                self.assertNotIn('DISPLAY', env)
                snapshot = json.loads(env[launcher.DESKTOP_CONTEXT])
                self.assertIsNone(snapshot['XDG_CONFIG_HOME'])
                self.assertNotIn('JAVA_TOOL_OPTIONS', snapshot)
            self.assertNotIn(launcher.DESKTOP_CONTEXT, original)

    def test_malformed_or_unapproved_desktop_context_is_rejected(self):
        for raw in ['not json', '[]', '{"LD_PRELOAD":"injected"}',
                    json.dumps({name: 42 for name in launcher.DESKTOP_VARIABLES})]:
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                launcher.desktop_environment({launcher.DESKTOP_CONTEXT: raw})

    def test_xwayland_control_and_manifest_classpath_guard(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'lib/app').mkdir(parents=True)
            cmd, env = launcher.command(root, root / 'profile', 'x11', False, {'DISPLAY': ':0'})
            self.assertIn('-Dawt.toolkit.name=XToolkit', cmd)
            self.assertIn('-Dabdm.experimental.nativeWayland=false', cmd)
            self.assertEqual(env['DISPLAY'], ':0')
            quit_cmd, _ = launcher.command(root, root / 'profile', 'x11', False, {'DISPLAY': ':0'}, quit_app=True)
            self.assertEqual(quit_cmd[-3:], ['com.abdownloadmanager.desktop.cli.CliAppKt', 'gui', 'exit'])
            self.assertIn(f'-Dabdm.experimental.profileRoot={root / "profile"}', quit_cmd)
            (root / 'lib/app/pathing-classpath.jar').touch()
            with self.assertRaises(ValueError):
                launcher.command(root, root / 'profile', 'x11', False, {'DISPLAY': ':0'})

    def test_download_fixture_requires_smoke_and_loopback_checksum(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            url = 'http://127.0.0.1:32456/abdm-native-smoke/payload.bin'
            cmd, _ = launcher.command(root, root / 'profile', 'wayland', True, {'WAYLAND_DISPLAY': 'wayland-0'}, url, 'a' * 64)
            self.assertIn('-Dabdm.experimental.smokeDownloadUrl=' + url, cmd)
            for fixture, checksum, smoke in [(url, 'a' * 64, False), (url.replace('127.0.0.1', 'example.com'), 'a' * 64, True),
                                              (url, 'invalid', True), (url + '?unexpected=yes', 'a' * 64, True)]:
                with self.subTest(fixture=fixture, checksum=checksum, smoke=smoke), self.assertRaises(ValueError):
                    launcher.command(root, root / 'profile', 'wayland', smoke, {'WAYLAND_DISPLAY': 'wayland-0'}, fixture, checksum)

    def test_bundle_changed_and_extra_files_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            payload = root / 'artifact.jar'
            payload.write_bytes(b'verified')
            manifest = {'kind': 'abdm-native-wayland-experiment-v1', 'files': {'artifact.jar': launcher.digest(payload)}, 'symlinks': {}}
            (root / 'bundle-manifest.json').write_text(json.dumps(manifest))
            launcher.verify(root)
            (root / 'unlisted.jar').touch()
            with self.assertRaises(ValueError):
                launcher.verify(root)
            (root / 'unlisted.jar').unlink()
            payload.write_bytes(b'changed')
            with self.assertRaises(ValueError):
                launcher.verify(root)

    def test_runtime_is_not_extracted_before_archive_verification(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            archive = root / 'unverified.tar.gz'
            archive.write_bytes(b'untrusted input')
            with self.assertRaises(ValueError):
                builder.extract_runtime(archive, root / 'runtime')
            self.assertFalse((root / 'runtime').exists())


if __name__ == '__main__':
    unittest.main()
