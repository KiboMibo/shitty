import tempfile
import os
import unittest
from pathlib import Path

from harness import Zutty


class FontResolverTest(unittest.TestCase):
    def test_missing_or_incompatible_double_width_font_keeps_primary_fallback(self):
        with tempfile.TemporaryDirectory() as directory:
            missing = str(Path(directory) / "missing")
            with Zutty() as terminal:
                primary = terminal.load_font(missing, "monospace", "")
                fallback = terminal.load_font(
                    missing,
                    "monospace",
                    "family-that-does-not-exist",
                )
            self.assertEqual(fallback["double_width"], 0)
            self.assertEqual(
                (fallback["px"], fallback["py"]),
                (primary["px"], primary["py"]),
            )

    def test_pcf_and_compressed_pcf_extensions_are_resolved(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            plain = base / "plain"
            compressed = base / "compressed"
            plain.mkdir()
            compressed.mkdir()
            (plain / "Bitmap-Regular.pcf").touch()
            (plain / "Bitmap-Bold.pcf").touch()
            (plain / "Bitmap-Italic.pcf.gz").touch()
            (compressed / "Packed-Regular.PCF.GZ").touch()
            (compressed / "Packed-Bold.pcf.gz").touch()
            (compressed / "Packed-Italic.pcf.zip").touch()

            with Zutty() as terminal:
                bitmap = terminal.resolve_font(str(base), "Bitmap")
                packed = terminal.resolve_font(str(base), "Packed")

            self.assertEqual(bitmap["regular"], str(plain / "Bitmap-Regular.pcf"))
            self.assertEqual(bitmap["bold"], str(plain / "Bitmap-Bold.pcf"))
            self.assertEqual(bitmap["italic"], "")
            self.assertEqual(
                packed["regular"], str(compressed / "Packed-Regular.PCF.GZ")
            )
            self.assertEqual(
                packed["bold"], str(compressed / "Packed-Bold.pcf.gz")
            )
            self.assertEqual(packed["italic"], "")

    def test_scaled_overlay_with_incompatible_metrics_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            with Zutty() as terminal:
                regular = terminal.resolve_fontconfig("Cascadia Code")["regular"]
                incompatible = terminal.resolve_fontconfig("Arial")["regular"]
            os.symlink(regular, base / "Clash-Regular.ttf")
            os.symlink(incompatible, base / "Clash-Bold.ttf")
            with Zutty() as terminal:
                loaded = terminal.load_font(str(base), "Clash", "")
            self.assertEqual(loaded["bold"], 0)

    def test_style_suffixes_are_classified_case_insensitively(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            names = {
                "regular": "Demo.ttf",
                "bold": "demo_B.TTF",
                "italic": "DEMO Oblique.ttf",
                "bold_italic": "Demo-BoldIt.ttf",
            }
            for name in names.values():
                (base / name).touch()
            with Zutty() as terminal:
                resolved = terminal.resolve_font(str(base), "dEmO")
            self.assertEqual(
                resolved,
                {key: str(base / value) for key, value in names.items()},
            )

    def test_fontconfig_loads_all_four_style_faces_when_available(self):
        with tempfile.TemporaryDirectory() as directory:
            with Zutty() as terminal:
                loaded = terminal.load_font(
                    str(Path(directory) / "missing"),
                    "IBM Plex Mono",
                    "",
                )
            self.assertEqual(
                (loaded["bold"], loaded["italic"], loaded["bold_italic"]),
                (1, 1, 1),
            )

    def test_fontconfig_fallback_loads_family_when_tree_has_no_match(self):
        with tempfile.TemporaryDirectory() as directory:
            missing = str(Path(directory) / "missing")
            with Zutty() as terminal:
                loaded = terminal.load_font(missing, "monospace", "")
            self.assertGreater(loaded["px"], 0)
            self.assertGreater(loaded["py"], 0)

    def test_font_path_traversal_finds_nested_family_after_missing_roots(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            incomplete = base / "incomplete"
            complete = base / "complete" / "nested"
            incomplete.mkdir()
            complete.mkdir(parents=True)
            (incomplete / "Demo-Bold.ttf").touch()
            expected = {}
            for style in ("Regular", "Bold", "Italic", "BoldItalic"):
                path = complete / f"Demo-{style}.ttf"
                path.touch()
                expected[style.lower()] = str(path)
            (complete / "Demo-Light.woff2").touch()
            (complete / "Unrelated-Regular.ttf").touch()

            search = ":".join(
                (str(base / "missing"), str(incomplete), str(base / "complete"))
            )
            with Zutty() as terminal:
                resolved = terminal.resolve_font(search, "Demo")

            self.assertEqual(resolved["regular"], expected["regular"])
            self.assertEqual(resolved["bold"], expected["bold"])
            self.assertEqual(resolved["italic"], expected["italic"])
            self.assertEqual(
                resolved["bold_italic"], expected["bolditalic"]
            )


if __name__ == "__main__":
    unittest.main()
