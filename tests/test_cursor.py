import unittest

from harness import Zutty


class CursorAndMovementTest(unittest.TestCase):
    def test_carriage_return_backspace_and_tab(self):
        with Zutty(columns=10, rows=2) as terminal:
            terminal.write(b"abc\rX\tY\bZ")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines[0], "Xbc     Z ")
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (9, 0))

    def test_absolute_and_relative_cursor_movement(self):
        with Zutty(columns=8, rows=4) as terminal:
            terminal.write(b"\x1b[3;4HX\x1b[2A\x1b[2DY")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(3, 2).char, "X")
            self.assertEqual(snapshot.cell(2, 0).char, "Y")

    def test_dec_save_and_restore_cursor(self):
        with Zutty(columns=8, rows=3) as terminal:
            terminal.write(b"\x1b[2;3H\x1b7\x1b[3;6HZ\x1b8X")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(2, 1).char, "X")
            self.assertEqual(snapshot.cell(5, 2).char, "Z")

    def test_sco_save_and_restore_cursor(self):
        with Zutty(columns=8, rows=3) as terminal:
            terminal.write(b"\x1b[2;3H\x1b[s\x1b[3;6HZ\x1b[uX")
            self.assertEqual(terminal.snapshot().cell(2, 1).char, "X")

    def test_scrolling_region_scrolls_without_moving_outer_rows(self):
        with Zutty(columns=5, rows=4) as terminal:
            terminal.write(
                b"AAAAA\r\nBBBBB\r\nCCCCC\r\nDDDDD"
                b"\x1b[2;3r\x1b[3;1H\n"
            )
            self.assertEqual(
                terminal.snapshot().lines,
                ["AAAAA", "CCCCC", "     ", "DDDDD"],
            )


if __name__ == "__main__":
    unittest.main()
