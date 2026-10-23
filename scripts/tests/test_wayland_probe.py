import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

SCRIPTS = Path(__file__).resolve().parents[1] / 'wayland-probe'


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / filename)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


native_ui = load('native_ui', 'native_ui.py')
runner = load('wayland_probe_runner', 'run.py')


class NativeUiArtifactTest(unittest.TestCase):
    def inputs(self, root):
        original = root / 'ui.jar'
        with zipfile.ZipFile(original, 'w') as jar:
            jar.writestr(native_ui.CLASS_PREFIX + '.class', b'original')
            jar.writestr(native_ui.CLASS_PREFIX + '$obsolete.class', b'old inner class')
            jar.writestr('androidx/compose/ui/Other.class', b'unchanged')
            jar.writestr('META-INF/ui.kotlin_module', b'upstream metadata')
        classes = root / 'classes'
        main = classes / (native_ui.CLASS_PREFIX + '.class')
        main.parent.mkdir(parents=True)
        main.write_bytes(b'patched')
        (classes / 'META-INF').mkdir()
        (classes / 'META-INF/main.kotlin_module').write_bytes(b'one-file metadata')
        return original, classes, root / 'experimental.jar'

    def test_unknown_source_and_binary_are_rejected_without_writing_output(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            original, classes, artifact = self.inputs(root)
            with self.assertRaises(ValueError):
                native_ui.build_jar(original, classes, artifact)
            self.assertFalse(artifact.exists())
            destination = root / 'source.kt'
            with self.assertRaises(ValueError):
                native_ui.prepare_source(original, destination)
            self.assertFalse(destination.exists())

    def test_jar_replaces_only_panel_family_preserving_upstream_metadata(self):
        with tempfile.TemporaryDirectory() as folder:
            original, classes, artifact = self.inputs(Path(folder))
            before = original.read_bytes()
            with patch.object(native_ui, 'BINARY_SHA256', native_ui.sha256(original)):
                with self.assertRaises(ValueError):
                    native_ui.build_jar(original, classes, original)
                manifest = native_ui.build_jar(original, classes, artifact)
                native_ui.validate_artifact(original, artifact, manifest)
                with zipfile.ZipFile(artifact) as jar:
                    self.assertEqual(jar.read(native_ui.CLASS_PREFIX + '.class'), b'patched')
                    self.assertNotIn(native_ui.CLASS_PREFIX + '$obsolete.class', jar.namelist())
                    self.assertEqual(jar.read('META-INF/ui.kotlin_module'), b'upstream metadata')
                    self.assertEqual(jar.read('androidx/compose/ui/Other.class'), b'unchanged')
                    self.assertNotIn('META-INF/main.kotlin_module', jar.namelist())
                    self.assertEqual(len(jar.namelist()), len(set(jar.namelist())))
                native_ui.build_jar(original, classes, artifact)
                self.assertEqual(native_ui.sha256(artifact), manifest['artifact_sha256'])
            self.assertEqual(original.read_bytes(), before)

    def test_unrelated_compiler_output_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            original, classes, artifact = self.inputs(Path(folder))
            (classes / 'Unexpected.class').write_bytes(b'not reviewed')
            with patch.object(native_ui, 'BINARY_SHA256', native_ui.sha256(original)):
                with self.assertRaises(ValueError):
                    native_ui.build_jar(original, classes, artifact)
            self.assertFalse(artifact.exists())

    def test_signed_or_duplicate_jar_is_rejected(self):
        for entry in ['META-INF/SIGNATURE.SF', 'META-INF/ui.kotlin_module']:
            with self.subTest(entry=entry), tempfile.TemporaryDirectory() as folder:
                original, classes, artifact = self.inputs(Path(folder))
                with zipfile.ZipFile(original, 'a') as jar:
                    if entry == 'META-INF/ui.kotlin_module':
                        with self.assertWarns(UserWarning):
                            jar.writestr(entry, b'duplicate')
                    else:
                        jar.writestr(entry, b'signed')
                with patch.object(native_ui, 'BINARY_SHA256', native_ui.sha256(original)):
                    with self.assertRaises(ValueError):
                        native_ui.build_jar(original, classes, artifact)
                self.assertFalse(artifact.exists())

    def test_changed_patch_or_artifact_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            original, classes, artifact = self.inputs(Path(folder))
            with patch.object(native_ui, 'BINARY_SHA256', native_ui.sha256(original)):
                manifest = native_ui.build_jar(original, classes, artifact)
                for key in ['patch_sha256', 'binary_sha256', 'source_sha256', 'compose']:
                    with self.subTest(key=key), self.assertRaises(ValueError):
                        native_ui.validate_artifact(original, artifact, {**manifest, key: 'changed'})
                artifact.write_bytes(artifact.read_bytes() + b'changed')
                with self.assertRaises(ValueError):
                    native_ui.validate_artifact(original, artifact, manifest)

    def test_launcher_manifest_jar_cannot_resolve_original_ui_first(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for name in ['ui-desktop-1.12.1.jar', 'app-utils.jar', 'pathing-classpath.jar']:
                (root / name).touch()
            self.assertEqual([p.name for p in native_ui.explicit_classpath_jars(root)],
                             ['app-utils.jar', 'ui-desktop-1.12.1.jar'])


class ProbeEvidenceTest(unittest.TestCase):
    source = 'file:/isolated/experimental-ui.jar'
    native = '''runtime=25.0.4.1+1-b635.72
        toolkit=sun.awt.wl.WLToolkit
        awtBackend=Wayland
        DISPLAY=null
        nativeRenderingOptIn=true
        composeWindowPanelSource=file:/isolated/experimental-ui.jar
        nativeX11Guard=true xOpenDisplayAttempted=false
        wl_surface#9.attach(wl_buffer#41, 0, 0)
        wl_surface#9.commit()
        POPUP_VIEWPORT_OK viewport=1400 x 920 layout=1400 x 920
        SMOKE_COMPLETE draws=20 updates=5
    '''

    def test_success_requires_toolkit_artifact_library_and_protocol_evidence(self):
        case = runner.Case('test', renderer='experimental', opt_in=True)
        self.assertEqual(runner.evidence_errors(case, 0, self.native, 'libawt_wlawt.so', self.source, ':0'), [])
        for marker in ['toolkit=sun.awt.wl.WLToolkit', 'awtBackend=Wayland', 'DISPLAY=null',
                       'nativeRenderingOptIn=true', self.source, 'nativeX11Guard=true',
                       '.attach(wl_buffer#', '.commit()', 'POPUP_VIEWPORT_OK', 'SMOKE_COMPLETE']:
            with self.subTest(marker=marker):
                self.assertTrue(runner.evidence_errors(case, 0, self.native.replace(marker, 'missing'),
                                                       'libawt_wlawt.so', self.source, ':0'))
        for maps in ['', 'libawt_xawt.so', 'libawt_wlawt.so libawt_xawt.so']:
            with self.subTest(maps=maps):
                self.assertTrue(runner.evidence_errors(case, 0, self.native, maps, self.source, ':0'))

    def test_known_failure_is_not_just_any_nonzero_exit(self):
        case = runner.Case('test', renderer='experimental', opt_in=True, expected=2)
        text = self.native.replace('SMOKE_COMPLETE', "SMOKE_FAILED\nCan't lock DrawingSurface")
        self.assertEqual(runner.evidence_errors(case, 2, text, '', self.source, None), [])
        self.assertTrue(runner.evidence_errors(case, 2, text.replace("Can't lock DrawingSurface", 'unrelated error'),
                                               '', self.source, None))
        self.assertTrue(runner.evidence_errors(case, 0, text, '', self.source, None))

    def test_mixed_environment_still_requires_real_wayland(self):
        case = runner.Case('test', renderer='experimental', opt_in=True, keep_display=True)
        self.assertTrue(runner.evidence_errors(case, 0, self.native, 'libawt_wlawt.so', self.source, ':0'))
        text = self.native.replace('DISPLAY=null', 'DISPLAY=:0')
        self.assertEqual(runner.evidence_errors(case, 0, text, 'libawt_wlawt.so', self.source, ':0'), [])
        self.assertTrue(runner.evidence_errors(case, 0, text.replace('wl.WLToolkit', 'X11.XToolkit'),
                                               'libawt_wlawt.so', self.source, ':0'))


if __name__ == '__main__':
    unittest.main()
