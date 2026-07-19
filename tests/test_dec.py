import unittest

from harness import Zutty


class DecProtocolTest(unittest.TestCase):
    def test_origin_mode_addresses_relative_to_vertical_margins(self):
        with Zutty(columns=8, rows=6) as terminal:
            terminal.write(b"\x1b[2;5r\x1b[?6hX\x1b[4;1HY")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(0, 1).char, "X")
            self.assertEqual(snapshot.cell(0, 4).char, "Y")

    def test_origin_mode_addresses_relative_to_both_margin_pairs(self):
        with Zutty(columns=10, rows=6) as terminal:
            terminal.write(
                b"\x1b[2;5r"
                b"\x1b[?69h"
                b"\x1b[3;8s"
                b"\x1b[?6h"
                b"X"
                b"\x1b[4;6H"
                b"Y"
            )
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(2, 1).char, "X")
            self.assertEqual(snapshot.cell(7, 4).char, "Y")

    def test_resetting_origin_mode_homes_cursor_absolutely(self):
        with Zutty(columns=8, rows=5) as terminal:
            terminal.write(b"\x1b[2;4r\x1b[?6h\x1b[?6lX")
            self.assertEqual(terminal.snapshot().cell(0, 0).char, "X")

    def test_vt52_cursor_addressing_and_device_attributes(self):
        with Zutty(columns=8, rows=5) as terminal:
            terminal.write(b"\x1b[?2l\x1bY\x22\x24X\x1bZ")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(4, 2).char, "X")
            self.assertEqual(terminal.read_input(), b"\x1b/Z")

    def test_vt52_can_return_to_ansi_mode(self):
        with Zutty(columns=8, rows=5) as terminal:
            terminal.write(b"\x1b[?2l\x1b<\x1b[2;3HX")
            self.assertEqual(terminal.snapshot().cell(2, 1).char, "X")

    def test_decaln_fills_screen_without_leaking_attributes(self):
        with Zutty(columns=5, rows=3) as terminal:
            terminal.write(b"\x1b[1;31m\x1b#8X")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, ["XEEEE", "EEEEE", "EEEEE"])
            self.assertTrue(snapshot.cell(0, 0).bold)
            self.assertFalse(snapshot.cell(1, 0).bold)

    def test_horizontal_tab_stop_set_and_clear(self):
        with Zutty(columns=12, rows=2) as terminal:
            terminal.write(b"\x1b[3g\x1b[1;4H\x1bH\r\tX\x1b[3g\r\tY")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(3, 0).char, "X")
            self.assertEqual(snapshot.cell(11, 0).char, "Y")

    def test_ris_resets_screen_modes_and_kitty_flags(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(
                b"text\x1b[?7l\x1b[?1003h\x1b[?1006h\x1b[>7u\x1bc"
            )
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, ["        ", "        "])
            self.assertEqual(terminal.state(), (0, 0, 0, 0))


if __name__ == "__main__":
    unittest.main()
