# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import tempfile
import unittest
from pathlib import Path

from font_fixture import make_font
from test_font_resolver import FontResolverTest
from harness import Shitty


ROOT = Path(__file__).resolve().parents[1]


def has_ink(pixels):
    return any(max(pixels[offset:offset + 3]) > 32 for offset in range(0, len(pixels), 3))


def chromatic_colors(pixels):
    return {
        pixels[offset:offset + 3]
        for offset in range(0, len(pixels), 3)
        if max(pixels[offset:offset + 3]) != min(pixels[offset:offset + 3])
    }


class FontFallbackTest(unittest.TestCase):
    def test_emoji_renders_in_color_out_of_the_box(self):
        with Shitty(columns=2, rows=1) as terminal:
            terminal.write("\x1b[?25l😀".encode())
            width, height, pixels = terminal.render_image("monospace")
        self.assertGreater(len(chromatic_colors(pixels)), 16)

    def test_zwj_cluster_renders_through_one_fallback_face(self):
        with Shitty(columns=2, rows=1) as terminal:
            terminal.write("\x1b[?25l👩‍💻".encode())
            width, height, pixels = terminal.render_image("monospace")
        self.assertGreater(len(chromatic_colors(pixels)), 16)

    def render_with_fixture(self, text):
        # A primary font that covers only "M" and U+3000, with blank
        # outlines: anything else must come from the embedded fallbacks.
        with tempfile.TemporaryDirectory(dir=ROOT / ".build") as directory:
            root = Path(directory)
            font = root / "fixture.ttf"
            font.write_bytes(
                make_font("Shitty Coverage Fixture", 500, 1000, 1000)
            )
            config = FontResolverTest.write_fontconfig(root)
            with Shitty(
                columns=4,
                rows=1,
                extra_environment={"FONTCONFIG_FILE": str(config)},
            ) as terminal:
                terminal.write(("\x1b[?25l" + text).encode())
                return terminal.render_image("Shitty Coverage Fixture")

    def test_narrow_codepoint_falls_back_by_coverage(self):
        width, height, pixels = self.render_with_fixture("W")
        self.assertTrue(has_ink(pixels))

    def test_codepoint_covered_nowhere_draws_a_box(self):
        width, height, pixels = self.render_with_fixture("͸")
        self.assertTrue(has_ink(pixels))


if __name__ == "__main__":
    unittest.main()
