# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import unittest

from harness import Shitty, put_rows


class SelectionTest(unittest.TestCase):
    def test_linear_selection_returns_utf8_text(self):
        with Shitty(columns=8, rows=3) as terminal:
            terminal.write(b"abc def\r\nghijk")
            terminal.select_start(1, 0)
            terminal.select_update(5, 0)
            self.assertEqual(terminal.snapshot().selection, (1, 0, 5, 0))
            self.assertEqual(terminal.select_finish(), b"bc d")

    def test_linear_selection_preserves_written_trailing_space(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"A ")
            terminal.select_start(0, 0)
            terminal.select_update(8, 0)
            self.assertEqual(terminal.select_finish(), b"A ")

    def test_rectangular_selection_returns_each_row_slice(self):
        with Shitty(columns=8, rows=3) as terminal:
            terminal.write(b"abc def\r\nghijk")
            terminal.select_start(1, 0)
            terminal.select_rectangular()
            terminal.select_update(3, 1)
            snapshot = terminal.snapshot()
            self.assertTrue(snapshot.rectangular_selection)
            self.assertEqual(terminal.select_finish(), b"bc\nhi")

    def test_selection_survives_output_while_view_is_scrolled(self):
        with Shitty(columns=8, rows=3, save_lines=8) as terminal:
            terminal.write(b"one\r\ntwo\r\nthree\r\nfour")
            terminal.page_up()
            terminal.select_start(0, 0)
            terminal.select_update(3, 0)
            terminal.write(b"\r\nfive")
            self.assertEqual(terminal.select_finish(), b"one")

    def test_selection_in_fixed_rows_survives_partial_scroll(self):
        with Shitty(columns=8, rows=6, save_lines=8) as terminal:
            terminal.write(put_rows(b"A", b"B", b"C", b"D", b"E", b"F"))
            terminal.select_start(0, 5)
            terminal.select_update(1, 5)

            terminal.write(b"\x1b[1;4r\x1b[S\x1b[r")

            self.assertEqual(terminal.snapshot().selection, (0, 5, 1, 5))
            self.assertEqual(terminal.select_finish(), b"F")

    def test_clearing_history_invalidates_history_selection(self):
        with Shitty(columns=8, rows=3, save_lines=8) as terminal:
            terminal.write(b"one\r\ntwo\r\nthree\r\nfour")
            terminal.page_up()
            terminal.select_start(0, 0)
            terminal.select_update(3, 0)

            terminal.write(b"\x1b[3J")

            self.assertEqual(terminal.snapshot().selection, (-1, -1, -1, -1))
            self.assertEqual(terminal.select_finish(), b"")

    def test_drag_to_bottom_right_window_edge_selects_last_row(self):
        with Shitty(columns=5, rows=2) as terminal:
            terminal.write(b"abcde\r\nfghij")

            terminal.button(0, True, x=2, y=2)
            terminal.pointer(x=7, y=6)

            self.assertEqual(
                terminal.button(0, False, x=7, y=6),
                b"abcde\nfghij",
            )

    def test_resize_clears_selection_clipped_outside_new_grid(self):
        with Shitty(columns=5, rows=3) as terminal:
            terminal.write(put_rows(b"A", b"B", b"C") + b"\x1b[H")
            terminal.select_start(0, 2)
            terminal.select_update(1, 2)

            terminal.resize(5, 2)

            self.assertEqual(terminal.snapshot().selection, (-1, -1, -1, -1))
            self.assertEqual(terminal.select_finish(), b"")


if __name__ == "__main__":
    unittest.main()
