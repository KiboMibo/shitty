import unittest

from harness import Zutty


class EditingTest(unittest.TestCase):
    def test_insert_and_delete_characters(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"abcdef\x1b[1;3H\x1b[2@XY")
            self.assertEqual(terminal.snapshot().lines[0], "abXYcdef")
            terminal.write(b"\x1b[1;3H\x1b[3P")
            self.assertEqual(terminal.snapshot().lines[0], "abdef   ")

    def test_erase_characters_and_line(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"abcdefgh\x1b[1;3H\x1b[3X")
            self.assertEqual(terminal.snapshot().lines[0], "ab   fgh")
            terminal.write(b"\x1b[1;5H\x1b[1K")
            self.assertEqual(terminal.snapshot().lines[0], "     fgh")

    def test_insert_and_delete_lines(self):
        with Zutty(columns=5, rows=4) as terminal:
            terminal.write(b"one\r\ntwo\r\nthree\r\nfour")
            terminal.write(b"\x1b[2;1H\x1b[L")
            self.assertEqual(
                terminal.snapshot().lines,
                ["one  ", "     ", "two  ", "three"],
            )
            terminal.write(b"\x1b[2;1H\x1b[M")
            self.assertEqual(
                terminal.snapshot().lines,
                ["one  ", "two  ", "three", "     "],
            )

    def test_insert_and_delete_lines_preserve_cursor_column(self):
        for operation in (b"\x1b[L", b"\x1b[M"):
            with self.subTest(operation=operation):
                with Zutty(columns=8, rows=4) as terminal:
                    terminal.write(b"one\r\ntwo\r\nthree\x1b[2;5H")
                    terminal.write(operation + b"X")
                    self.assertEqual(terminal.snapshot().cell(4, 1).char, "X")

    def test_erase_display(self):
        with Zutty(columns=5, rows=3) as terminal:
            terminal.write(b"one\r\ntwo\r\nthree\x1b[2;2H\x1b[J")
            self.assertEqual(terminal.snapshot().lines, ["one  ", "t    ", "     "])

    def test_insert_mode(self):
        with Zutty(columns=6, rows=2) as terminal:
            terminal.write(b"abcd\x1b[1;2H\x1b[4hX\x1b[4lY")
            self.assertEqual(terminal.snapshot().lines[0], "aXYcd ")

    def test_scroll_up_and_down(self):
        with Zutty(columns=5, rows=4) as terminal:
            terminal.write(b"one\r\ntwo\r\nthree\r\nfour\x1b[2S")
            self.assertEqual(
                terminal.snapshot().lines,
                ["three", "four ", "     ", "     "],
            )
        with Zutty(columns=5, rows=4) as terminal:
            terminal.write(b"one\r\ntwo\r\nthree\r\nfour\x1b[2T")
            self.assertEqual(
                terminal.snapshot().lines,
                ["     ", "     ", "one  ", "two  "],
            )

    def test_large_scroll_count_is_clamped_to_region(self):
        with Zutty(columns=5, rows=3) as terminal:
            terminal.write(b"one\r\ntwo\r\nthree\x1b[999S")
            self.assertEqual(terminal.snapshot().lines, ["     "] * 3)
            terminal.write(b"one\r\ntwo\r\nthree\x1b[999T")
            self.assertEqual(terminal.snapshot().lines, ["     "] * 3)

    def test_scroll_left_and_right(self):
        with Zutty(columns=4, rows=2) as terminal:
            terminal.write(b"abcd\x1b[2;1Hefgh\x1b[2 @")
            self.assertEqual(terminal.snapshot().lines, ["cd  ", "gh  "])
        with Zutty(columns=4, rows=2) as terminal:
            terminal.write(b"abcd\x1b[2;1Hefgh\x1b[2 A")
            self.assertEqual(terminal.snapshot().lines, ["  ab", "  ef"])

    def test_repeat_preceding_graphic_character(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"A\x1b[5b")
            self.assertEqual(terminal.snapshot().lines[0], "AAAAAA  ")

    def test_forward_and_backward_tabulation(self):
        with Zutty(columns=20, rows=2) as terminal:
            terminal.write(b"\x1b[2IX\x1b[20G\x1b[2ZY")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(16, 0).char, "X")
            self.assertEqual(snapshot.cell(8, 0).char, "Y")

    def test_insert_and_delete_columns(self):
        with Zutty(columns=6, rows=2) as terminal:
            terminal.write(b"abcdef\x1b[2;1Hghijkl\x1b[1;3H\x1b[2'}")
            self.assertEqual(terminal.snapshot().lines, ["ab  cd", "gh  ij"])
            terminal.write(b"\x1b[2'~")
            self.assertEqual(terminal.snapshot().lines, ["abcd  ", "ghij  "])

    def test_erase_saved_lines_discards_scrollback(self):
        with Zutty(columns=8, rows=3, save_lines=8) as terminal:
            terminal.write(b"one\r\ntwo\r\nthree\r\nfour\x1b[3J")
            terminal.page_up()
            self.assertEqual(terminal.snapshot().view_offset, 0)

    def test_erase_saved_lines_preserves_live_screen(self):
        with Zutty(columns=8, rows=3, save_lines=8) as terminal:
            terminal.write(b"one\r\ntwo\r\nthree\r\nfour")
            before = terminal.snapshot().lines
            terminal.write(b"\x1b[3J")

            self.assertEqual(terminal.snapshot().lines, before)


if __name__ == "__main__":
    unittest.main()
