import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parents[1] / 'native-wayland'
spec = importlib.util.spec_from_file_location('native_integration_unit', HERE / 'desktop_integration.py')
integration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(integration)
launcher = integration.installer.load_launcher()


class NativeIntegrationTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.home = root / 'home'
        self.home.mkdir()
        self.bundle = root / 'bundle'
        for name in ['bin/ABDownloadManagerNativeWayland', 'runtime/bin/java']:
            file = self.bundle / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text('#!/bin/sh\nexit 0\n')
            file.chmod(0o755)
        files = {str(p.relative_to(self.bundle)): integration.installer.sha(p) for p in self.bundle.rglob('*') if p.is_file()}
        self.manifest = self.bundle / 'bundle-manifest.json'
        self.metadata = {'kind': 'abdm-native-wayland-experiment-v1', 'version': '1.11.0+native.2',
                         'desktop_integration': True, 'files': files, 'symlinks': {}}
        self.manifest.write_text(json.dumps(self.metadata))
        self.icon = root / 'icon.png'
        self.icon.write_bytes(b'fixture icon')
        self.wrapper = self.home / '.local/bin/ABDownloadManager'
        self.wrapper.parent.mkdir(parents=True)
        self.wrapper.write_text('original XWayland wrapper')
        self.firefox = integration.manifest_paths(self.home)[0]
        self.firefox.parent.mkdir(parents=True)
        self.firefox.write_text('{"path":"/original/host"}')

    def configure(self):
        # Fixture bundles are never executed. No global cache tools or processes run.
        with patch.object(integration.subprocess, 'run'):
            return integration.configure(self.bundle, self.icon, self.home, refresh=False)

    def test_takeover_uses_quiet_native_host_and_separate_data(self):
        state = self.configure()
        base, profile = integration.paths(self.home)
        self.assertIn('ABDownloadManagerNativeWayland', self.wrapper.read_text())
        host = self.home / '.local/bin/ABDownloadManagerNativeWaylandHost'
        self.assertIn('--native-host', host.read_text())
        self.assertIn(str(profile), host.read_text())
        self.assertIn(f'home={self.home}', (profile / integration.MARKER).read_text())
        data = json.loads((profile / 'app-data/config/appSettings.json').read_text())
        self.assertTrue(data['apiEnabled'] and data['useSystemTray'] and data['autoStartOnBoot'])
        self.assertEqual(data['apiPort'], 15151)
        self.assertTrue((Path(state['backup']) / 'restore-integrations.sh').is_file())

    def test_fake_home_manifest_install_receives_its_desktop_environment(self):
        env = {'HOME': str(self.home), 'XDG_DATA_HOME': str(self.home / 'custom-data')}
        with patch.object(integration.subprocess, 'run') as run:
            integration.configure(self.bundle, self.icon, self.home, refresh=False, environment=env)
        calls = [call for call in run.call_args_list if call.args[0][-2:] == ['native-messaging', 'install']]
        self.assertEqual(len(calls), 1)
        self.assertEqual(calls[0].kwargs['env'], env)

    def test_unsupported_bundle_is_rejected_before_stopping_or_writing(self):
        self.metadata.pop('desktop_integration')
        self.manifest.write_text(json.dumps(self.metadata))
        with patch.object(integration, 'stop_if_running') as stop:
            with self.assertRaises(ValueError):
                self.configure()
            stop.assert_not_called()
        self.assertEqual(self.wrapper.read_text(), 'original XWayland wrapper')

    def test_active_downloads_block_takeover(self):
        file = self.home / '.abdm/config/download_db/downloadlist/1.json'
        file.parent.mkdir(parents=True)
        file.write_text('{"status":"Downloading"}')
        with patch.object(integration, 'stop_if_running') as stop:
            with self.assertRaises(RuntimeError):
                self.configure()
            stop.assert_not_called()
        self.assertEqual(self.wrapper.read_text(), 'original XWayland wrapper')

    def test_unrelated_dedicated_host_is_not_overwritten(self):
        host = self.home / '.local/bin/ABDownloadManagerNativeWaylandHost'
        host.write_text('unrelated command')
        with self.assertRaises(ValueError):
            self.configure()
        self.assertEqual(host.read_text(), 'unrelated command')

    def test_rollback_restores_integration_but_preserves_native_history(self):
        state = self.configure()
        _, profile = integration.paths(self.home)
        history = profile / 'app-data/config/history.json'
        history.write_text('new native history')
        with patch.object(integration.installer, 'refresh_desktop'):
            integration.restore(Path(state['backup']))
        self.assertEqual(self.wrapper.read_text(), 'original XWayland wrapper')
        self.assertEqual(self.firefox.read_text(), '{"path":"/original/host"}')
        self.assertEqual(history.read_text(), 'new native history')
        self.assertFalse((profile / integration.MARKER).exists())

    def test_rollback_refuses_later_wrapper_edits(self):
        state = self.configure()
        self.wrapper.write_text('later custom command')
        with self.assertRaises(ValueError):
            integration.restore(Path(state['backup']))
        self.assertEqual(self.wrapper.read_text(), 'later custom command')

    def test_cli_host_background_and_smoke_modes_are_separate(self):
        _, profile = integration.paths(self.home)
        cmd, env = launcher.command(self.bundle, profile, 'wayland', False, {}, native_args=['origin'])
        self.assertEqual(cmd[-2:], ['com.abdownloadmanager.desktop.nativemessaging.host.NativeMessagingHostKt', 'origin'])
        self.assertNotIn('DISPLAY', env)
        cli, _ = launcher.command(self.bundle, profile, 'wayland', False, {}, cli_args=['gui', 'exit'])
        self.assertEqual(cli[-3:], ['com.abdownloadmanager.desktop.cli.CliAppKt', 'gui', 'exit'])
        with self.assertRaises(ValueError):
            launcher.command(self.bundle, profile, 'wayland', False, {}, native_args=[], background=True)
        profile.mkdir(parents=True)
        (profile / integration.MARKER).write_text('explicit activation')
        with self.assertRaises(ValueError):
            launcher.command(self.bundle, profile, 'wayland', True, {'WAYLAND_DISPLAY': 'wayland-0'})


if __name__ == '__main__':
    unittest.main()
