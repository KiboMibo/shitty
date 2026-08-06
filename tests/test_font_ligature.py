# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import tempfile
import unittest
from pathlib import Path

import test_font_resolver

from font_fixture import make_liga_font
from harness import Shitty
from test_font_resolver import FONTCONFIG_AVAILABLE, ROOT


def cell_ink(pixels, width, border, cell_width, cell_height, column):
    total = 0
    left = border + column * cell_width
    for y in range(border, border + cell_height):
        for x in range(left, left + cell_width):
            offset = 3 * (y * width + x)
            total += max(pixels[offset : offset + 3])
    return total


class FontLigatureTest(unittest.TestCase):
    # Issue 57: the fixture carries an fi ligature in its `liga` feature,
    # one narrow glyph over two codepoints. A terminal must not let the
    # shaper apply it: "fi" would collapse into the f cell and leave the
    # i cell blank.
    @unittest.skipUnless(FONTCONFIG_AVAILABLE, "Fontconfig is not available")
    def test_typographic_ligature_does_not_collapse_cells(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            font = root / "liga.ttf"
            font.write_bytes(make_liga_font("Shitty Liga Fixture", 500))
            config = test_font_resolver.FontResolverTest.write_fontconfig(root)
            with Shitty(
                columns=4,
                rows=1,
                extra_environment={"FONTCONFIG_FILE": str(config)},
            ) as terminal:
                terminal.write(b"\x1b[?25lfi")
                metrics = terminal.load_font("Shitty Liga Fixture")
                border = terminal.options()["border"]
                width, height, pixels = terminal.render_image("Shitty Liga Fixture")
        for column in (0, 1):
            self.assertGreater(
                cell_ink(
                    pixels,
                    width,
                    border,
                    metrics["px"],
                    metrics["py"],
                    column,
                ),
                0,
                f"cell {column} of 'fi' lost its ink",
            )


if __name__ == "__main__":
    unittest.main()
