# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import unittest
from pathlib import Path

from harness import Shitty


ROOT = Path(__file__).resolve().parents[1]
NERD_FONT = ROOT / "fonts" / "JetBrainsMonoNerdFont-Regular.ttf"

# Wide Nerd Font pictograms behind width-one codepoints, the eza --icons
# staple: the NixOS snowflake, the Python logo, and the C symbol from the
# issue-32 listing.
ICONS = ("", "", "")


def ink_margins(pixels, width, cell_width, height, cell):
    """Horizontal ink margins of one cell in an RGB row-major image."""
    left = None
    right = None
    for column in range(cell * cell_width, (cell + 1) * cell_width):
        for row in range(height):
            offset = (row * width + column) * 3
            if max(pixels[offset : offset + 3]) > 8:
                inside = column - cell * cell_width
                left = inside if left is None else min(left, inside)
                right = inside if right is None else max(right, inside)
                break
    if left is None:
        return None
    return left, cell_width - 1 - right


class NerdIconRenderTest(unittest.TestCase):
    def test_wide_icons_fit_their_cell(self):
        for icon in ICONS:
            with self.subTest(icon=hex(ord(icon))):
                with Shitty(
                    columns=4,
                    rows=1,
                    extra_arguments=("-fontsize", "32"),
                ) as terminal:
                    terminal.write(b"\x1b[?25l " + icon.encode() + b"  ")
                    width, height, pixels = terminal.render_image(NERD_FONT)

                cell_width = width // 4
                margins = ink_margins(pixels, width, cell_width, height, 1)
                self.assertIsNotNone(margins, "icon rendered no ink")
                left, right = margins
                ink = cell_width - left - right
                # A trimmed glyph is flush against the cell edge; a fitted
                # one keeps clearance on both sides and stays substantial.
                self.assertGreaterEqual(left, 1, f"ink flush left ({margins})")
                self.assertGreaterEqual(right, 1, f"ink flush right ({margins})")
                self.assertGreaterEqual(ink, cell_width // 2, "icon shrank away")
                # The neighbouring cells stay clean: fitting, not overflow.
                for neighbour in (0, 2):
                    self.assertIsNone(
                        ink_margins(pixels, width, cell_width, height, neighbour),
                        f"ink leaked into cell {neighbour}",
                    )


if __name__ == "__main__":
    unittest.main()
