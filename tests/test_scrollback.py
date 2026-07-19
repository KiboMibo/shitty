import unittest

from harness import Zutty


class ScrollbackTest(unittest.TestCase):
    def test_new_output_preserves_scrolled_view(self):
        with Zutty(columns=8, rows=3, save_lines=8) as terminal:
            terminal.write(b"one\r\ntwo\r\nthree\r\nfour")
            terminal.page_up()

            before = terminal.snapshot()
            self.assertEqual(before.view_offset, 1)
            self.assertEqual(before.lines, ["one     ", "two     ", "three   "])

            terminal.write(b"\r\nfive")

            after = terminal.snapshot()
            self.assertEqual(after.view_offset, 2)
            self.assertEqual(after.lines, ["one     ", "two     ", "three   "])

    def test_alternate_screen_keeps_scrollback(self):
        with Zutty(columns=8, rows=3, save_lines=8) as terminal:
            terminal.write(
                b"\x1b[?1049h"
                b"one\r\ntwo\r\nthree\r\nfour"
            )
            terminal.page_up()

            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.view_offset, 1)
            self.assertEqual(
                snapshot.lines,
                ["one     ", "two     ", "three   "],
            )

            terminal.write(b"\r\nfive")

            after = terminal.snapshot()
            self.assertEqual(after.view_offset, 2)
            self.assertEqual(
                after.lines,
                ["one     ", "two     ", "three   "],
            )


if __name__ == "__main__":
    unittest.main()
