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

    def test_erase_display(self):
        with Zutty(columns=5, rows=3) as terminal:
            terminal.write(b"one\r\ntwo\r\nthree\x1b[2;2H\x1b[J")
            self.assertEqual(terminal.snapshot().lines, ["one  ", "t    ", "     "])

    def test_insert_mode(self):
        with Zutty(columns=6, rows=2) as terminal:
            terminal.write(b"abcd\x1b[1;2H\x1b[4hX\x1b[4lY")
            self.assertEqual(terminal.snapshot().lines[0], "aXYcd ")


if __name__ == "__main__":
    unittest.main()
