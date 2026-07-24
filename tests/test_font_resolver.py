# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import os
import unittest
from pathlib import Path

from harness import Shitty


ROOT = Path(__file__).resolve().parents[1]
COLOR_FONT = ROOT / "tests" / "fonts" / "NotoColorEmoji.ttf"


class FontResolverTest(unittest.TestCase):
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
