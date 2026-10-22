"""System-theme architecture checks.

Contrast is tested on resolved Kirigami colors in the CTest
DownloadsViewTest::desktopThemeAndReviewGallery, including semantic text and
focus rings on every custom row background. A regex over literal hex tokens
cannot measure a desktop-provided palette.
"""
from pathlib import Path
import re
import unittest

THEME = (Path(__file__).resolve().parents[3] / "desktop/qt-ui/qml/PreviewTheme.qml").read_text()


class PreviewContrastTest(unittest.TestCase):
    def test_uses_system_palette_and_font_without_literal_color_tokens(self):
        self.assertIn("Kirigami.Theme.Window", THEME)
        self.assertIn("Kirigami.Theme.View", THEME)
        self.assertIn("Kirigami.Theme.defaultFont", THEME)
        self.assertNotRegex(THEME, r'property color \w+:\s*"#[a-fA-F0-9]+"')

    def test_semantic_text_and_custom_focus_use_contrast_guard(self):
        for role, threshold in (("secondary", "4.5"), ("positive", "4.5"), ("warning", "4.5"),
                                ("error", "4.5"), ("focusColor", "3.0")):
            with self.subTest(role=role):
                self.assertRegex(THEME, rf'property color {role}: readableColor\(.+, {re.escape(threshold)}\)')

    def test_palette_provider_is_not_hidden(self):
        self.assertNotRegex(THEME, r"visible:\s*false")
        self.assertNotRegex(THEME, r"opacity:\s*0\b")


if __name__ == "__main__":
    unittest.main()
