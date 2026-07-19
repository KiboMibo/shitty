import unittest

from harness import Zutty


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


if __name__ == "__main__":
    unittest.main()
