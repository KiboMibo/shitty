import unittest

from harness import Zutty


class UnicodeTest(unittest.TestCase):
    def test_utf8_decoder_survives_every_byte_boundary(self):
        text = "aé界z".encode()
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write_chunks(*(bytes([byte]) for byte in text))
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines[0][:5], "aé界 z")
            self.assertTrue(snapshot.cell(2, 0).double_width)

    def test_dec_special_graphics_charset(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b(0lqk\x1b(B")
            self.assertEqual(terminal.snapshot().lines[0][:3], "┌─┐")

    def test_single_shift_uses_selected_charset_once(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b*0\x1bNqx")
            self.assertEqual(terminal.snapshot().lines[0][:2], "─x")


if __name__ == "__main__":
    unittest.main()
