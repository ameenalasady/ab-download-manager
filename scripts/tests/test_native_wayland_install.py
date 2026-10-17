import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'native-wayland/install.py'
spec = importlib.util.spec_from_file_location('native_desktop_install', SCRIPT)
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class NativeDesktopInstallTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.home = self.root / 'home'
        self.home.mkdir()
        self.bundle = self.root / 'bundle'
        self.bundle.mkdir()
        for name in ['bin/ABDownloadManagerNativeWayland', 'runtime/bin/java']:
            file = self.bundle / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text('#!/bin/sh\nexit 0\n')
            file.chmod(0o755)
        files = {str(p.relative_to(self.bundle)): installer.sha(p) for p in self.bundle.rglob('*') if p.is_file()}
        (self.bundle / 'bundle-manifest.json').write_text(json.dumps({
            'kind': 'abdm-native-wayland-experiment-v1', 'version': '1.11.0+native.1', 'files': files, 'symlinks': {},
        }))
        self.icon = self.root / 'icon.png'
        self.icon.write_bytes(b'PNG fixture')
        self.desktop = self.home / '.local/share/applications/com.abdownloadmanager.desktop'
        self.desktop.parent.mkdir(parents=True)
        self.previous = '[Desktop Entry]\nType=Application\nName=Original\nExec=/original\n'
        self.desktop.write_text(self.previous)
        self.protected = []
        for name in ['.local/bin/ABDownloadManager', '.local/bin/ABDownloadManagerCli',
                     '.local/bin/ABDownloadManagerNativeMessagingHost', '.abdm/config/appSettings.json',
                     '.config/autostart/com.abdownloadmanager.desktop',
                     '.mozilla/native-messaging-hosts/com.abdownloadmanager.json',
                     '.local/share/kwin-wayland/effects/maximize/contents/code/main.js']:
            path = self.home / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('preserve ' + name)
            self.protected.append((path, path.read_bytes()))

    def install(self):
        return installer.install(self.bundle, self.icon, self.home, refresh=False)

    def restore(self, record):
        # No global desktop-cache tools are reachable in these disposable tests.
        path = self.root / 'commands'
        path.mkdir(exist_ok=True)
        (path / 'python3').symlink_to(sys.executable)
        env = dict(os.environ, PATH=str(path), HOME=str(self.home))
        return subprocess.run(['/bin/sh', str(Path(record['backup']) / 'restore-desktop.sh')],
                              env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    def test_copies_immutable_bundle_and_activates_only_desktop(self):
        record = self.install()
        release = Path(record['release'])
        installer.load_launcher().verify(release)
        self.assertFalse(release.is_relative_to(self.bundle))
        self.assertIn('ABDownloadManagerNativeWayland', self.desktop.read_text())
        self.assertIn('StartupWMClass=com.abdownloadmanager.desktop.experimental.NativeWaylandAppKt', self.desktop.read_text())
        self.assertEqual((Path(record['backup']) / 'previous.desktop').read_text(), self.previous)
        settings = json.loads((Path(record['profile']) / 'app-data/config/appSettings.json').read_text())
        self.assertEqual(settings['defaultDownloadFolder'], str(self.home / 'Downloads/ABDM-Native-Wayland'))
        self.assertFalse(settings['apiEnabled'])
        self.assertFalse(settings['autoStartOnBoot'])
        for file, content in self.protected:
            self.assertEqual(file.read_bytes(), content)

    def test_reinstall_preserves_native_preferences_and_history(self):
        record = self.install()
        profile = Path(record['profile'])
        settings = profile / 'app-data/config/appSettings.json'
        settings.write_text('{"customPreference": true}')
        history = profile / 'app-data/config/history.json'
        history.write_text('keep this download history')
        self.install()
        self.assertEqual(settings.read_text(), '{"customPreference": true}')
        self.assertEqual(history.read_text(), 'keep this download history')

    def test_restore_is_exact_and_keeps_native_files(self):
        record = self.install()
        result = self.restore(record)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(self.desktop.read_text(), self.previous)
        self.assertTrue(Path(record['release']).is_dir())
        self.assertTrue(Path(record['profile']).is_dir())

    def test_restore_refuses_later_desktop_edits(self):
        record = self.install()
        self.desktop.write_text('my later desktop change')
        result = self.restore(record)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.desktop.read_text(), 'my later desktop change')

    def test_restore_handles_no_previous_override(self):
        self.desktop.unlink()
        record = self.install()
        result = self.restore(record)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertFalse(self.desktop.exists())

    def test_unowned_installation_or_wrapper_is_rejected(self):
        base = self.home / '.local/opt/ab-download-manager-native-wayland'
        base.mkdir(parents=True)
        (base / 'unrelated').write_text('keep')
        with self.assertRaises(ValueError):
            self.install()
        self.assertEqual(self.desktop.read_text(), self.previous)
        (base / 'unrelated').unlink()
        wrapper = self.home / '.local/bin/ABDownloadManagerNativeWayland'
        wrapper.write_text('my unrelated wrapper')
        with self.assertRaises(ValueError):
            self.install()
        self.assertEqual(wrapper.read_text(), 'my unrelated wrapper')
        self.assertEqual(self.desktop.read_text(), self.previous)

    def test_changed_owned_wrapper_is_rejected(self):
        record = self.install()
        wrapper = self.home / '.local/bin/ABDownloadManagerNativeWayland'
        wrapper.write_text('my modified native wrapper')
        with self.assertRaises(ValueError):
            self.install()
        self.assertEqual(wrapper.read_text(), 'my modified native wrapper')
        self.assertEqual(installer.sha(self.desktop), record['desktop_sha256'])

    def test_symlinked_desktop_is_not_followed(self):
        self.desktop.unlink()
        target = self.root / 'unrelated'
        target.write_text('keep original')
        self.desktop.symlink_to(target)
        with self.assertRaises(ValueError):
            self.install()
        self.assertEqual(target.read_text(), 'keep original')


if __name__ == '__main__':
    unittest.main()
