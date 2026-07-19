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

    def test_supplementary_plane_codepoint_is_preserved(self):
        with Zutty(columns=6, rows=2) as terminal:
            terminal.write("😀X".encode())
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(0, 0).char, "😀")
            self.assertTrue(snapshot.cell(0, 0).double_width)
            self.assertEqual(snapshot.cell(2, 0).char, "X")

            terminal.select_start(0, 0)
            terminal.select_update(2, 0)
            self.assertEqual(terminal.select_finish(), "😀".encode())

    def test_invalid_utf8_is_replaced_without_aliasing_unicode(self):
        cases = (
            (b"\xc0\xafX", "��X"),
            (b"\xed\xa0\x80X", "�X"),
            (b"\xf4\x90\x80\x80X", "�X"),
            (b"\xf0\x80\x80\x80X", "�X"),
            (b"\xf5\x80\x80\x80X", "����X"),
        )
        for encoded, expected in cases:
            with self.subTest(encoded=encoded):
                with Zutty(columns=8, rows=2) as terminal:
                    terminal.write(encoded)
                    self.assertEqual(
                        terminal.snapshot().lines[0][: len(expected)],
                        expected,
                    )


if __name__ == "__main__":
    unittest.main()
