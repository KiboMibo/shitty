import tempfile
import os
import unittest
from pathlib import Path

from harness import Zutty


class FontResolverTest(unittest.TestCase):
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
