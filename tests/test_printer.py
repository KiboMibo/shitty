import unittest

from harness import Zutty


class PrinterProtocolTest(unittest.TestCase):
    def test_media_copy_prints_screen_and_current_line(self):
        with Zutty(columns=5, rows=3) as terminal:
            terminal.write(b"one\r\ntwo\x1b[?1i")
            self.assertEqual(terminal.read_printer(), b"two\n")

            terminal.write(b"\x1b[0i")
            self.assertEqual(terminal.read_printer(), b"one\ntwo\n\n")

    def test_autoprint_prints_line_when_cursor_leaves_it(self):
        with Zutty(columns=5, rows=3) as terminal:
            terminal.write(b"\x1b[?5iabc\r\ndef\x1b[?4i")
            self.assertEqual(terminal.read_printer(), b"abc\n")

    def test_printer_controller_copies_stream_without_displaying_it(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"before\x1b[5ihello\r\nworld\x1b[4iafter")
            self.assertEqual(terminal.read_printer(), b"hello\r\nworld")
            self.assertEqual(terminal.snapshot().lines[0], "beforeaf")


if __name__ == "__main__":
    unittest.main()
