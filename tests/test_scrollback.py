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

    def test_partial_region_scroll_does_not_create_blank_history(self):
        with Zutty(columns=8, rows=6, save_lines=20) as terminal:
            terminal.write(
                b"screen1\r\nscreen2\r\nscreen3"
                b"\x1b[3;6r\x1b[3;1H\x1b[5S\x1b[r"
            )
            terminal.wheel_up()

            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.view_offset, 0)

    def test_codex_partial_reverse_index_preserves_real_history(self):
        with Zutty(columns=8, rows=6, save_lines=20) as terminal:
            terminal.write(
                b"line1\r\nline2\r\nline3\r\nline4\r\n"
                b"line5\r\nline6\r\nline7\r\nline8\r\n"
                b"line9\r\nline10\r\nline11\r\nline12"
            )
            terminal.wheel_up()
            terminal.wheel_up()
            before = terminal.snapshot()
            self.assertEqual(before.view_offset, 6)
            self.assertEqual(
                before.lines,
                ["line1   ", "line2   ", "line3   ",
                 "line4   ", "line5   ", "line6   "],
            )
            terminal.wheel_down()
            terminal.wheel_down()

            terminal.write(
                b"\x1b[?2026h"
                b"\x1b[3;6r\x1b[3;1H"
                + b"\x1bM" * 8
                + b"\x1b[r"
                b"\x1b[?2026l"
            )
            terminal.wheel_up()
            terminal.wheel_up()

            after = terminal.snapshot()
            self.assertEqual(after.view_offset, 6)
            self.assertEqual(after.lines, before.lines)

    def test_codex_full_screen_reverse_index_preserves_real_history(self):
        with Zutty(columns=8, rows=6, save_lines=20) as terminal:
            terminal.write(
                b"line1\r\nline2\r\nline3\r\nline4\r\n"
                b"line5\r\nline6\r\nline7\r\nline8\r\n"
                b"line9\r\nline10\r\nline11\r\nline12"
            )
            terminal.wheel_up()
            terminal.wheel_up()
            before = terminal.snapshot()
            terminal.wheel_down()
            terminal.wheel_down()

            terminal.write(
                b"\x1b[?2026h"
                b"\x1b[1;6r\x1b[1;1H"
                + b"\x1bM" * 8
                + b"\x1b[r"
                b"\x1b[?2026l"
            )
            terminal.wheel_up()
            terminal.wheel_up()

            after = terminal.snapshot()
            self.assertEqual(after.view_offset, 6)
            self.assertEqual(after.lines, before.lines)


if __name__ == "__main__":
    unittest.main()
