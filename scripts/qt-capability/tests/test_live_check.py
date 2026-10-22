"""Padding-evidence validation tests; no live desktop or capture access."""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

try:
    import live_check
except ImportError:
    live_check = None


GEOMETRY = {
    "geometry": [300, 100, 980, 840],
    "backdrop_geometry": [100, 50, 1408, 1034],
    "pad": 8,
    "pixels_per_logical": 1.0,
}


@unittest.skipUnless(live_check, "numpy/Pillow and the capability launcher are needed")
class PaddingValidationTest(unittest.TestCase):
    def test_accepts_declared_solid_paddings(self):
        for pattern, rgb in (("black", [0.0, 0.0, 0.0]),
                             ("white", [255.0, 255.0, 255.0]),
                             ("magenta", [255.0, 0.0, 255.0]),
                             ("black", [8.0, 8.0, 8.0])):
            with self.subTest(pattern=pattern, rgb=rgb):
                live_check.validate_exposed_padding(pattern, rgb, GEOMETRY)

    def test_rejects_solid_padding_beyond_tolerance(self):
        with self.assertRaisesRegex(RuntimeError, "Evidence contamination"):
            live_check.validate_exposed_padding("black", [9.0, 0.0, 0.0], GEOMETRY)

    def test_accepts_deterministic_stripe_padding(self):
        # rendered-final-3: shell x=300, pad=8, backdrop x=100, width 1408 -> stripe 1.
        self.assertEqual(live_check.expected_padding_rgb("stripes", GEOMETRY), [230, 127, 51])
        live_check.validate_exposed_padding("stripes", [230.0, 127.0, 51.0], GEOMETRY)

    def test_rejects_stripe_padding_from_other_pattern(self):
        with self.assertRaisesRegex(RuntimeError, "Evidence contamination"):
            live_check.validate_exposed_padding("stripes", [0.0, 0.0, 0.0], GEOMETRY)

    def test_rejects_nonfinite_padding(self):
        with self.assertRaisesRegex(RuntimeError, "non-finite"):
            live_check.validate_exposed_padding("white", [float("nan"), 255.0, 255.0], GEOMETRY)

    def test_rejects_wrong_number_of_channels(self):
        for channels in ([], [0, 0], [0, 0, 0, 0]):
            with self.subTest(channels=channels), self.assertRaisesRegex(RuntimeError, "three"):
                live_check.validate_exposed_padding("black", channels, GEOMETRY)

    def test_stripes_require_capture_geometry(self):
        with self.assertRaisesRegex(RuntimeError, "without capture geometry"):
            live_check.validate_exposed_padding("stripes", [230.0, 127.0, 51.0], {})

    def test_move_revalidates_identity_before_every_move(self):
        calls = []

        def record_owned(*args):
            calls.append(("owned", args))

        def record_run(*args):
            calls.append(("run", args))

        with patch.object(live_check, "owned", side_effect=record_owned), \
             patch.object(live_check, "run", side_effect=record_run):
            live_check.move_owned("window", 7, "app", 100, 50)
        self.assertEqual(calls, [("owned", ("window", 7, "app")),
                                 ("run", ("kdotool", "windowmove", "window", "100", "50"))])


if __name__ == "__main__":
    unittest.main()
