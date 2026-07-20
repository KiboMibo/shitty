import tempfile
import unittest
from pathlib import Path

from harness import Zutty


class FontResolverTest(unittest.TestCase):
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
