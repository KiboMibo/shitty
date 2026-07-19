import unittest

from harness import Zutty, put_rows


class SelectionTest(unittest.TestCase):
    def test_linear_selection_returns_utf8_text(self):
        with Zutty(columns=8, rows=3) as terminal:
            terminal.write(b"abc def\r\nghijk")
            terminal.select_start(1, 0)
            terminal.select_update(5, 0)
            self.assertEqual(terminal.snapshot().selection, (1, 0, 5, 0))
            self.assertEqual(terminal.select_finish(), b"bc d")

    def test_rectangular_selection_returns_each_row_slice(self):
        with Zutty(columns=8, rows=3) as terminal:
            terminal.write(b"abc def\r\nghijk")
            terminal.select_start(1, 0)
            terminal.select_rectangular()
            terminal.select_update(3, 1)
            snapshot = terminal.snapshot()
            self.assertTrue(snapshot.rectangular_selection)
            self.assertEqual(terminal.select_finish(), b"bc\nhi")

    def test_selection_survives_output_while_view_is_scrolled(self):
        with Zutty(columns=8, rows=3, save_lines=8) as terminal:
            terminal.write(b"one\r\ntwo\r\nthree\r\nfour")
            terminal.page_up()
            terminal.select_start(0, 0)
            terminal.select_update(3, 0)
            terminal.write(b"\r\nfive")
            self.assertEqual(terminal.select_finish(), b"one")

    def test_selection_in_fixed_rows_survives_partial_scroll(self):
        with Zutty(columns=8, rows=6, save_lines=8) as terminal:
            terminal.write(put_rows(b"A", b"B", b"C", b"D", b"E", b"F"))
            terminal.select_start(0, 5)
            terminal.select_update(1, 5)

            terminal.write(b"\x1b[1;4r\x1b[S\x1b[r")

            self.assertEqual(terminal.snapshot().selection, (0, 5, 1, 5))
            self.assertEqual(terminal.select_finish(), b"F")

    def test_clearing_history_invalidates_history_selection(self):
        with Zutty(columns=8, rows=3, save_lines=8) as terminal:
            terminal.write(b"one\r\ntwo\r\nthree\r\nfour")
            terminal.page_up()
            terminal.select_start(0, 0)
            terminal.select_update(3, 0)

            terminal.write(b"\x1b[3J")

            self.assertEqual(terminal.snapshot().selection, (-1, -1, -1, -1))
            self.assertEqual(terminal.select_finish(), b"")


if __name__ == "__main__":
    unittest.main()
