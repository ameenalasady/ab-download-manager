"""Token-level AA checks; not a rendered all-states accessibility certification."""
from pathlib import Path
import re
import unittest

QML = (Path(__file__).resolve().parents[3] / "desktop/qt-capability/qml/Main.qml").read_text()


def rgb(value):
    return tuple(int(value[i:i+2],16) for i in (0,2,4))


def luminance(color):
    linear = [v/255/12.92 if v/255 <= 0.04045 else ((v/255+0.055)/1.055)**2.4 for v in color]
    return sum(v*w for v,w in zip(linear,(0.2126,0.7152,0.0722)))


def contrast(a,b):
    light,dark = sorted((luminance(a),luminance(b)),reverse=True)
    return (light+0.05)/(dark+0.05)


class ContrastTest(unittest.TestCase):
    def test_inputs_follow_desktop_style_not_bespoke_placeholder_tokens(self):
        # Real resolved desktop foreground/background contrasts are tested by
        # tst_downloadsview. The synthetic probe must not override styled inputs.
        self.assertNotIn('placeholderTextColor:', QML)
        self.assertNotIn('MultiEffect', QML)
        self.assertNotIn('objectName: "closeButton"', QML)
        self.assertNotIn('objectName: "maximizeButton"', QML)

    def test_synthetic_foreground_on_white_backdrop_material(self):
        # Synthetic probe only; the real view samples its active native style.
        self.assertIn('"#cc1b1b24"', QML)
        self.assertIn('color: "white"', QML)
        bright = tuple(round(0.8*v+0.2*255) for v in rgb("1B1B24"))
        self.assertGreaterEqual(contrast(rgb("FFFFFF"), bright), 4.5)


if __name__ == "__main__":
    unittest.main()
