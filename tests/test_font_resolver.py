# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import os
import tempfile
import unittest
from pathlib import Path

from font_fixture import make_collection, make_font
from harness import Shitty


ROOT = Path(__file__).resolve().parents[1]
COLOR_FONT = ROOT / "tests" / "fonts" / "NotoColorEmoji.ttf"


class FontResolverTest(unittest.TestCase):
    @staticmethod
    def write_fontconfig(root):
        config = root / "fonts.conf"
        config.write_text(
            "<?xml version=\"1.0\"?>\n"
            "<!DOCTYPE fontconfig SYSTEM \"urn:fontconfig:fonts.dtd\">\n"
            "<fontconfig>\n"
            f"  <dir>{root}</dir>\n"
            f"  <cachedir>{root / 'cache'}</cachedir>\n"
            "  <config><rescan><int>0</int></rescan></config>\n"
            "</fontconfig>\n"
        )
        return config

    def test_collection_face_and_representative_advances_define_cells(self):
        collection = make_collection(
            make_font("Shitty Wrong Face", 750, 1500, 2000),
            make_font("Shitty Test Mono", 500, 1000, 3000),
        )
        with tempfile.TemporaryDirectory(dir=ROOT / ".build") as directory:
            root = Path(directory)
            font = root / "fixture.ttc"
            font.write_bytes(collection)
            config = self.write_fontconfig(root)
            with Shitty(
                extra_environment={"FONTCONFIG_FILE": str(config)}
            ) as terminal:
                variants = terminal.resolve_fontconfig("Shitty Test Mono")
                loaded = terminal.load_font(
                    "Shitty Test Mono",
                    "Shitty Test Mono",
                )

        self.assertEqual(variants["regular"], str(font))
        self.assertEqual(variants["regular_index"], 1)
        self.assertEqual(loaded["px"], 8)
        self.assertEqual(loaded["double_width"], 1)

    def test_overlay_width_is_independent_but_vertical_metrics_must_match(self):
        family = "Shitty Overlay Test"
        collection = make_collection(
            make_font(family, 500, 1000, 500),
            make_font(family, 700, 1400, 700, style="Bold"),
            make_font(
                family,
                500,
                1000,
                500,
                descender=-300,
                style="Italic",
            ),
            make_font(
                family,
                500,
                1000,
                500,
                ascender=700,
                descender=-300,
                style="Bold Italic",
            ),
        )
        with tempfile.TemporaryDirectory(dir=ROOT / ".build") as directory:
            root = Path(directory)
            font = root / "overlay.ttc"
            font.write_bytes(collection)
            config = self.write_fontconfig(root)
            with Shitty(
                extra_environment={"FONTCONFIG_FILE": str(config)}
            ) as terminal:
                variants = terminal.resolve_fontconfig(family)
                loaded = terminal.load_font(family, "")

        self.assertEqual(variants["regular"], str(font))
        self.assertEqual(variants["bold"], str(font))
        self.assertEqual(variants["italic"], str(font))
        self.assertEqual(variants["bold_italic"], str(font))
        self.assertEqual(variants["regular_index"], 0)
        self.assertEqual(variants["bold_index"], 1)
        self.assertEqual(variants["italic_index"], 2)
        self.assertEqual(variants["bold_italic_index"], 3)
        self.assertEqual(loaded["bold"], 1)
        self.assertEqual(loaded["italic"], 0)
        self.assertEqual(loaded["bold_italic"], 0)

    def test_missing_or_incompatible_double_width_font_keeps_primary_fallback(self):
        with Shitty() as terminal:
            primary = terminal.load_font("monospace", "")
            fallback = terminal.load_font("monospace", "Arial")
        self.assertEqual(fallback["double_width"], 0)
        self.assertEqual(
            (fallback["px"], fallback["py"]),
            (primary["px"], primary["py"]),
        )

    def test_fontconfig_resolves_family_and_alias_to_existing_files(self):
        with Shitty() as terminal:
            for family in ("monospace", "IBM Plex Mono"):
                with self.subTest(family=family):
                    variants = terminal.resolve_fontconfig(family)
                    self.assertTrue(variants["regular"])
                    self.assertTrue(os.path.isfile(variants["regular"]))

    def test_fontconfig_loads_all_four_style_faces_when_available(self):
        with Shitty() as terminal:
            loaded = terminal.load_font("IBM Plex Mono", "")
        self.assertEqual(
            (loaded["bold"], loaded["italic"], loaded["bold_italic"]),
            (1, 1, 1),
        )

    def test_fontconfig_family_loads_without_a_search_path(self):
        with Shitty() as terminal:
            loaded = terminal.load_font("monospace", "")
        self.assertGreater(loaded["px"], 0)
        self.assertGreater(loaded["py"], 0)

    def test_font_file_path_is_not_treated_as_a_family(self):
        with Shitty(extra_arguments=("-fontsize", "32")) as terminal:
            variants = terminal.resolve_fontconfig(COLOR_FONT)
            primary = terminal.load_font(COLOR_FONT, "")
            fallback = terminal.load_font("monospace", COLOR_FONT)
        self.assertEqual(variants["regular"], str(COLOR_FONT))
        self.assertLessEqual(primary["py"], 64)
        self.assertEqual(fallback["double_width"], 1)


if __name__ == "__main__":
    unittest.main()
