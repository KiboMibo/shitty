# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

"""XTPUSHSGR/XTPOPSGR with explicit attribute selections: a pop
restores only what the push named and leaves the rest of the live
rendition alone."""

import unittest

from harness import Shitty


RED = (0xAA, 0x00, 0x00)
BLUE = (0x00, 0x00, 0xAA)


class SgrStackTest(unittest.TestCase):
    def test_selective_pop_restores_named_attributes_only(self):
        with Shitty(columns=8, rows=2) as terminal:
            # Bold and italic are pushed; the foreground is not, so the
            # pop keeps the blue set after the push.
            terminal.write(
                b"\x1b[1;3;31m"
                b"\x1b[1;3#{"
                b"\x1b[22;23;34m"
                b"\x1b[#}A"
            )
            cell = terminal.snapshot().cell(0, 0)
            self.assertTrue(cell.bold)
            self.assertTrue(cell.italic)
            self.assertEqual(cell.foreground, BLUE)

    def test_selective_color_pop_restores_both_grounds(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(
                b"\x1b[31m"
                b"\x1b[30;31#{"
                b"\x1b[34;43m"
                b"\x1b[#}A"
            )
            snapshot = terminal.snapshot()
            cell = snapshot.cell(0, 0)
            plain = snapshot.cell(1, 0)
            self.assertEqual(cell.foreground, RED)
            self.assertEqual(cell.background, plain.background)

    def test_selective_underline_pop_restores_the_style(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(
                b"\x1b[4:3m"
                b"\x1b[4#{"
                b"\x1b[24m"
                b"\x1b[#}A"
            )
            self.assertEqual(terminal.snapshot().cell(0, 0).underline_style, 3)

    def test_pop_of_an_empty_stack_changes_nothing(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[31m\x1b[#}A")
            self.assertEqual(terminal.snapshot().cell(0, 0).foreground, RED)


if __name__ == "__main__":
    unittest.main()
