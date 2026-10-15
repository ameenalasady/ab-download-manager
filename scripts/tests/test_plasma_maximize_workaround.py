import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'plasma-maximize-workaround.py'
spec = importlib.util.spec_from_file_location('plasma_maximize_workaround', SCRIPT)
workaround = importlib.util.module_from_spec(spec)
spec.loader.exec_module(workaround)

# Minimal stock handler preserves the exact guards used by the installed effect.
SOURCE = '''
class MaximizeEffect {
    onWindowFrameGeometryChanged(window, oldGeometry) {
        if (!window.maximizeAnimation1 ||
            (window.geometry.width == oldGeometry.width && window.geometry.height == oldGeometry.height) ||
            (window.oldGeometry.width == oldGeometry.width && window.oldGeometry.height == oldGeometry.height)
        ) return;
        cancel(window.maximizeAnimation1);
        delete window.maximizeAnimation1;
        if (window.maximizeAnimation2) {
            cancel(window.maximizeAnimation2);
            delete window.maximizeAnimation2;
        }
    }
}
'''


class MaximizeWorkaroundTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('node'), 'JavaScript behavior test requires node')
    def test_only_roundoff_for_abdm_ignores_resize_cancellation(self):
        program = workaround.build_script(SOURCE) + '''
const assert = require('node:assert/strict');
let cancelled = [];
function cancel(id) { cancelled.push(id); }
function invoke(cls, width, previousWidth, savedWidth=2048) {
    cancelled = [];
    const w = { windowClass: cls, geometry: {width, height: 581.6},
        oldGeometry: {width: savedWidth, height: 1152},
        maximizeAnimation1: 1, maximizeAnimation2: 2 };
    new MaximizeEffect().onWindowFrameGeometryChanged(w, {width: previousWidth, height: 581.6});
    return cancelled;
}
const abdm = ''' + json.dumps(workaround.CLASS) + ''';
assert.deepEqual(invoke(abdm, 1120.8, 1120.8000000000002), []);
assert.deepEqual(invoke(abdm, 1120.8, 1120.8), []);
assert.deepEqual(invoke(abdm, 1121.6, 1120.8), [1, 2]); // real one-X-pixel resize
assert.deepEqual(invoke(abdm, 1120.8, 1120.80001), [1, 2]); // above epsilon
assert.deepEqual(invoke('konsole org.kde.konsole', 1120.8, 1120.8000000000002), [1, 2]);
assert.deepEqual(invoke('other app', 1120.8, 1120.8), []);
'''
        subprocess.run(['node', '-e', program], check=True, capture_output=True, text=True)

    def test_unknown_upstream_code_is_not_patched(self):
        with self.assertRaises(ValueError):
            workaround.build_script('class ChangedEffect {}')
        with self.assertRaises(ValueError):
            workaround.build_script(SOURCE + SOURCE)

    def source(self, root):
        source = root / 'stock'
        (source / 'contents/code').mkdir(parents=True)
        (source / 'contents/code/main.js').write_text(SOURCE)
        (source / 'metadata.json').write_text(json.dumps({'KPlugin': {'Id': 'maximize'}}))
        return source

    def test_install_refresh_and_remove_owned_copy(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source, target = self.source(root), root / 'local'
            workaround.install(source, target)
            workaround.check_owned(target)
            (source / 'contents/code/main.js').write_text(SOURCE + '// updated stock comment\n')
            workaround.install(source, target)
            self.assertIn('updated stock comment', (target / 'contents/code/main.js').read_text())
            workaround.remove(target)
            self.assertFalse(target.exists())
            workaround.remove(target)  # idempotent

    def test_unrelated_override_is_never_overwritten_or_removed(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source, target = self.source(root), root / 'local'
            target.mkdir()
            custom = target / 'custom.js'
            custom.write_text('user override')
            with self.assertRaises(ValueError):
                workaround.install(source, target)
            with self.assertRaises(ValueError):
                workaround.remove(target)
            self.assertEqual('user override', custom.read_text())

    def test_edited_owned_copy_is_not_overwritten_or_removed(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source, target = self.source(root), root / 'local'
            workaround.install(source, target)
            (target / 'contents/code/main.js').write_text('user edits')
            with self.assertRaises(ValueError):
                workaround.install(source, target)
            with self.assertRaises(ValueError):
                workaround.remove(target)

    def test_symlink_override_is_not_followed(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source, target = self.source(root), root / 'local'
            target.symlink_to(source, target_is_directory=True)
            with self.assertRaises(ValueError):
                workaround.install(source, target)
            self.assertEqual(SOURCE, (source / 'contents/code/main.js').read_text())


if __name__ == '__main__':
    unittest.main()
