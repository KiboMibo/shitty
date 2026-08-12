# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

"""A non-scalable bitmap strike through the FreeType backend: the
strike's own ascender places the baseline, and the monochrome glyphs
land in the cell bit for bit."""

import unittest

from font_fixture import FONT_ROOT
from harness import Shitty


BITMAP_FONT = FONT_ROOT / "fixture-8x8.bdf"
BORDER = 2

GLYPH_A = (0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x00)
GLYPH_B = (0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00)


def cell_bits(pixels, width, column):
    # A lit pixel is whatever differs from the corner background; the
    # mono strike leaves no antialiasing in between.
    background = pixels[:3]
    rows = []
    for y in range(8):
        value = 0
        for x in range(8):
            offset = 3 * ((BORDER + y) * width + BORDER + column * 8 + x)
            value = (value << 1) | int(pixels[offset : offset + 3] != background)
        rows.append(value)
    return tuple(rows)


class BitmapFontRenderTest(unittest.TestCase):
    def render(self, size):
        with Shitty(
            columns=2,
            rows=1,
            extra_arguments=("-fontsize", str(size)),
        ) as terminal:
            terminal.write(b"\x1b[?25lAB")
            metrics = terminal.load_font(str(BITMAP_FONT))
            width, height, pixels = terminal.render_image(str(BITMAP_FONT))
            return metrics, width, height, pixels

    def test_matching_size_draws_the_strike_bit_for_bit(self):
        metrics, width, height, pixels = self.render(8)
        self.assertEqual((metrics["px"], metrics["py"]), (8, 8))
        self.assertEqual((width, height), (2 * BORDER + 16, 2 * BORDER + 8))
        self.assertEqual(cell_bits(pixels, width, 0), GLYPH_A)
        self.assertEqual(cell_bits(pixels, width, 1), GLYPH_B)

    def test_mismatched_size_still_uses_the_only_strike(self):
        metrics, width, height, pixels = self.render(16)
        # A bitmap-only face has nothing to scale: the lone 8-pixel
        # strike serves whatever size was asked.
        self.assertEqual((metrics["px"], metrics["py"]), (8, 8))
        self.assertEqual(cell_bits(pixels, width, 0), GLYPH_A)


if __name__ == "__main__":
    unittest.main()
