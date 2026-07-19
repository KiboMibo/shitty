import unittest

from harness import Zutty


class TerminalModeTest(unittest.TestCase):
    def test_decll_tracks_each_host_led_independently(self):
        with Zutty() as terminal:
            terminal.write(b"\x1b[1;2;3q\x1b[22q")
            self.assertEqual(terminal.protocol_state()[1], 0b101)
            terminal.write(b"\x1b[q")
            self.assertEqual(terminal.protocol_state()[1], 0)

    def test_decscreen_mode_reverses_the_composed_display(self):
        with Zutty() as terminal:
            terminal.write(b"\x1b[?5h")
            self.assertEqual(terminal.protocol_state()[0], 1)
            terminal.write(b"\x1b[?5l")
            self.assertEqual(terminal.protocol_state()[0], 0)

    def test_meta_mode_sets_the_eighth_input_bit(self):
        with Zutty() as terminal:
            terminal.write(b"\x1b[?1034h")
            terminal.char("a", modifiers=4)
            self.assertEqual(terminal.read_input(), b"\xe1")
            terminal.write(b"\x1b[?1034l")
            terminal.char("a", modifiers=4)
            self.assertEqual(terminal.read_input(), b"\x1ba")

    def test_reverse_wrap_follows_soft_wrapped_lines_only(self):
        with Zutty(columns=4, rows=3) as terminal:
            terminal.write(b"abcdX\x1b[?45h\x1b[2;1H\bY")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(3, 0).char, "Y")

            terminal.write(b"\x1b[3;1H\bZ")
            self.assertEqual(terminal.snapshot().cell(0, 2).char, "Z")

    def test_extended_reverse_wrap_crosses_hard_line_boundaries(self):
        with Zutty(columns=4, rows=3) as terminal:
            terminal.write(b"\x1b[?45h\x1b[?1045h\x1b[3;1H\x1b[2DY")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(2, 1).char, "Y")


if __name__ == "__main__":
    unittest.main()
