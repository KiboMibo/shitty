import unittest

from harness import Zutty


class MouseProtocolTest(unittest.TestCase):
    def test_default_encoding_press_and_release(self):
        with Zutty(columns=8, rows=2) as terminal:
            self.assertEqual(
                terminal.mouse_encode(0, 0, 0, 0, 1, 2, 3),
                b'\x1b[M "#',
            )
            self.assertEqual(
                terminal.mouse_encode(0, 1, 0, 0, 1, 2, 3),
                b'\x1b[M#"#',
            )

    def test_utf8_encoding_supports_large_coordinates(self):
        with Zutty(columns=8, rows=2) as terminal:
            self.assertEqual(
                terminal.mouse_encode(1, 0, 0, 0, 1, 200, 300),
                b"\x1b[M \xc3\xa8\xc5\x8c",
            )

    def test_sgr_encoding_distinguishes_press_release_and_motion(self):
        with Zutty(columns=8, rows=2) as terminal:
            self.assertEqual(
                terminal.mouse_encode(2, 0, 7, 0, 1, 12, 7),
                b"\x1b[<28;12;7M",
            )
            self.assertEqual(
                terminal.mouse_encode(2, 1, 0, 0, 3, 12, 7),
                b"\x1b[<2;12;7m",
            )
            self.assertEqual(
                terminal.mouse_encode(2, 2, 0, 1, 0, 12, 7),
                b"\x1b[<32;12;7M",
            )
            self.assertEqual(
                terminal.mouse_encode(2, 2, 0, 0, 0, 12, 7),
                b"\x1b[<35;12;7M",
            )

    def test_sgr_wheel_and_extended_buttons(self):
        with Zutty(columns=8, rows=2) as terminal:
            self.assertEqual(
                terminal.mouse_encode(2, 0, 0, 0, 4, 1, 1),
                b"\x1b[<64;1;1M",
            )
            self.assertEqual(
                terminal.mouse_encode(2, 0, 0, 0, 8, 1, 1),
                b"\x1b[<128;1;1M",
            )
            self.assertEqual(
                terminal.mouse_encode(2, 0, 0, 0, 10, 1, 1),
                b"\x1b[<130;1;1M",
            )
            self.assertEqual(
                terminal.mouse_encode(2, 0, 0, 0, 11, 1, 1),
                b"\x1b[<131;1;1M",
            )

    def test_sgr_pixel_encoding_and_mode(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[?1016h\x1b[?1016$p")
            self.assertEqual(terminal.state()[1], 4)
            self.assertEqual(terminal.read_input(), b"\x1b[?1016;1$y")
            self.assertEqual(
                terminal.mouse_encode(4, 0, 0, 0, 1, 321, 123),
                b"\x1b[<0;321;123M",
            )

    def test_legacy_coordinate_encodings_are_clamped(self):
        with Zutty(columns=8, rows=2) as terminal:
            self.assertEqual(
                terminal.mouse_encode(0, 0, 0, 0, 1, 999, -4),
                b"\x1b[M \xff!",
            )
            self.assertEqual(
                terminal.mouse_encode(1, 0, 0, 0, 1, 9999, -4),
                b"\x1b[M \xdf\xbf!",
            )

    def test_urxvt_encoding(self):
        with Zutty(columns=8, rows=2) as terminal:
            self.assertEqual(
                terminal.mouse_encode(3, 0, 4, 0, 2, 9, 4),
                b"\x1b[49;9;4M",
            )

    def test_invalid_button_is_ignored(self):
        with Zutty(columns=8, rows=2) as terminal:
            self.assertEqual(
                terminal.mouse_encode(2, 0, 0, 0, 12, 1, 1),
                b"",
            )


if __name__ == "__main__":
    unittest.main()
