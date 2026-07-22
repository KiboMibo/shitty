import unittest

from harness import Shitty


class PrinterProtocolTest(unittest.TestCase):
    def test_media_copy_prints_screen_and_current_line(self):
        with Shitty(columns=5, rows=3) as terminal:
            terminal.write(b"one\r\ntwo\x1b[?1i")
            self.assertEqual(terminal.read_printer(), b"two\n")

            terminal.write(b"\x1b[0i")
            self.assertEqual(terminal.read_printer(), b"one\ntwo\n\n")

    def test_autoprint_prints_line_when_cursor_leaves_it(self):
        with Shitty(columns=5, rows=3) as terminal:
            terminal.write(b"\x1b[?5iabc\r\ndef\x1b[?4i")
            self.assertEqual(terminal.read_printer(), b"abc\n")

    def test_print_modes_select_extent_and_form_feed(self):
        with Shitty(columns=5, rows=4) as terminal:
            terminal.write(
                b"one\x1b[2;1Htwo\x1b[3;1Hthree\x1b[4;1Hfour"
                b"\x1b[2;3r\x1b[?19l\x1b[?18h"
                b"\x1b[?18$p\x1b[?19$p\x1b[0i"
            )
            self.assertEqual(
                terminal.read_input(),
                b"\x1b[?18;1$y\x1b[?19;2$y",
            )
            self.assertEqual(terminal.read_printer(), b"two\nthree\n\f")

            terminal.write(b"\x1b[?18l\x1b[?19h\x1b[0i")
            self.assertEqual(
                terminal.read_printer(),
                b"one\ntwo\nthree\nfour\n",
            )

    def test_printer_controller_copies_stream_without_displaying_it(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"before\x1b[5ihello\r\nworld\x1b[4iafter")
            self.assertEqual(terminal.read_printer(), b"hello\r\nworld")
            self.assertEqual(terminal.snapshot().lines[0], "beforeaf")

    def test_printer_controller_tracks_terminators_across_writes(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"before\x1b[5ihello\x1b[")
            terminal.write(b"4iafter")
            self.assertEqual(terminal.read_printer(), b"hello")
            self.assertEqual(terminal.snapshot().lines[0], "beforeaf")

    def test_printer_controller_preserves_false_terminator_prefixes(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"before\x1b[5ia\x1bxb\x9bxc\x9b" b"4iafter")
            self.assertEqual(terminal.read_printer(), b"a\x1bxb\x9bxc")
            self.assertEqual(terminal.snapshot().lines[0], "beforeaf")


if __name__ == "__main__":
    unittest.main()
