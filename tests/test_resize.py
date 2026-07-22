import unittest

from harness import Shitty


class ResizeTest(unittest.TestCase):
    def test_growing_reflows_cells_and_cursor(self):
        with Shitty(columns=5, rows=2) as terminal:
            terminal.write(b"abcde123")
            terminal.resize(7, 3)
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.columns, snapshot.rows), (7, 3))
            self.assertEqual(snapshot.lines, ["abcde12", "3      ", "       "])
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (1, 1))
            self.assertEqual(terminal.winsize(), (7, 3))

    def test_shrinking_reflows_cells_and_cursor_into_history(self):
        with Shitty(columns=7, rows=3, save_lines=4) as terminal:
            terminal.write(b"abcde\r\n12345")
            terminal.resize(4, 2)
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, ["1234", "5   "])
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (1, 1))
            terminal.wheel_up(4)
            self.assertEqual(terminal.snapshot().lines, ["abcd", "e   "])

    def test_resize_alternate_screen_and_restore_primary(self):
        with Shitty(columns=5, rows=2) as terminal:
            terminal.write(b"main\x1b[?1049halt")
            terminal.resize(7, 3)
            terminal.write(b"\x1b[?1049l")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.columns, snapshot.rows), (7, 3))
            self.assertEqual(snapshot.lines[0], "main   ")

    def test_initial_pty_winsize_matches_grid(self):
        with Shitty(columns=11, rows=7) as terminal:
            self.assertEqual(terminal.winsize(), (11, 7))


if __name__ == "__main__":
    unittest.main()
