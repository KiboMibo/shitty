import unittest

from harness import Zutty


class ResizeTest(unittest.TestCase):
    def test_growing_preserves_cells_and_cursor(self):
        with Zutty(columns=5, rows=2) as terminal:
            terminal.write(b"abcde123")
            terminal.resize(7, 3)
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.columns, snapshot.rows), (7, 3))
            self.assertEqual(snapshot.lines, ["abcde  ", "123    ", "       "])
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (3, 1))

    def test_shrinking_clips_cells_and_cursor(self):
        with Zutty(columns=7, rows=3) as terminal:
            terminal.write(b"abcde\r\n12345")
            terminal.resize(4, 2)
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, ["abcd", "1234"])
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (3, 1))

    def test_resize_alternate_screen_and_restore_primary(self):
        with Zutty(columns=5, rows=2) as terminal:
            terminal.write(b"main\x1b[?1049halt")
            terminal.resize(7, 3)
            terminal.write(b"\x1b[?1049l")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.columns, snapshot.rows), (7, 3))
            self.assertEqual(snapshot.lines[0], "main   ")


if __name__ == "__main__":
    unittest.main()
