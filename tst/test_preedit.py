# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import unittest

from harness import Shitty


class PreeditTest(unittest.TestCase):
    def test_preview_overlays_the_cursor_row(self):
        with Shitty(columns=10, rows=3) as terminal:
            terminal.write(b"ab")
            terminal.preedit("ni", 0, 2)
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines[0][:4], "abni")
            self.assertTrue(snapshot.cell(2, 0).inverse)
            self.assertTrue(snapshot.cell(3, 0).inverse)
            terminal.preedit("")
            self.assertEqual(terminal.snapshot().lines[0][:4], "ab  ")

    def test_preview_outside_cursor_range_is_underlined(self):
        with Shitty(columns=10, rows=3) as terminal:
            terminal.preedit("abc", -1, -1)
            snapshot = terminal.snapshot()
            self.assertTrue(snapshot.cell(0, 0).underline)
            self.assertFalse(snapshot.cell(0, 0).inverse)

    def test_preview_hides_the_regular_cursor_and_anchors_it(self):
        with Shitty(columns=10, rows=3) as terminal:
            terminal.write(b"ab")
            terminal.preedit("xy", 2, 2)
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cursor_style, 0)
            self.assertEqual(snapshot.cursor_x, 4)
            self.assertEqual(snapshot.cursor_y, 0)

    def test_preview_never_reaches_the_model_or_the_pty(self):
        with Shitty(columns=10, rows=3) as terminal:
            terminal.write(b"ab")
            terminal.preedit("XYZ", -1, -1)
            self.assertEqual(terminal.all_text()[0], "ab")
            self.assertEqual(terminal.read_input(), b"")

    def test_wide_preview_shifts_left_at_the_right_edge(self):
        with Shitty(columns=6, rows=2) as terminal:
            terminal.write(b"\x1b[1;5H")
            terminal.preedit("漢字", -1, -1)
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(2, 0).char, "漢")
            self.assertEqual(snapshot.cell(4, 0).char, "字")

    def test_oversized_preview_keeps_the_tail_visible(self):
        with Shitty(columns=4, rows=2) as terminal:
            terminal.preedit("abcdef", -1, -1)
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines[0], "cdef")


if __name__ == "__main__":
    unittest.main()
