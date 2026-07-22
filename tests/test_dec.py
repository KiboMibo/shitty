import unittest

from harness import Shitty


class DecProtocolTest(unittest.TestCase):
    def test_double_width_and_double_height_line_attributes_persist(self):
        with Shitty(columns=8, rows=3) as terminal:
            terminal.write(b"AB\x1b#6\x1b[2K")
            snapshot = terminal.snapshot()
            self.assertTrue(all(snapshot.cell(x, 0).line_attribute == 3 for x in range(8)))

            terminal.write(b"\x1b[2;1Htop\x1b#3\x1b[3;1Hbottom\x1b#4")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(0, 1).line_attribute, 1)
            self.assertEqual(snapshot.cell(0, 2).line_attribute, 2)

            terminal.write(b"\x1b[1;1H\x1b#5")
            self.assertEqual(terminal.snapshot().cell(0, 0).line_attribute, 0)

    def test_reverse_index_creates_single_width_lines(self):
        with Shitty(columns=8, rows=4) as terminal:
            terminal.write(
                b"\x1b[2;1H\x1b#3"
                b"\x1b[3;1H\x1b#4"
                b"\x1b[4;1H\x1b#6"
                b"\x1b[2;4r\x1b[2;1H\x1bM"
            )
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(0, 1).line_attribute, 0)
            self.assertEqual(snapshot.cell(0, 2).line_attribute, 1)
            self.assertEqual(snapshot.cell(0, 3).line_attribute, 2)

    def test_ed_resets_fully_erased_line_attributes(self):
        with Shitty(columns=8, rows=3) as terminal:
            terminal.write(b"\x1b[2;1H\x1b#3\x1b[2J")
            snapshot = terminal.snapshot()
            self.assertTrue(all(
                snapshot.cell(0, row).line_attribute == 0
                for row in range(3)
            ))

    def test_writing_to_double_width_line_clamps_absolute_cursor_position(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b#6\x1b[8GAB")
            self.assertEqual(terminal.snapshot().lines, ["   A    ", "B       "])

    def test_repeat_on_double_width_line_does_not_overflow_cursor(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b#6\x1b[8GA\x1b[2b")
            self.assertEqual(terminal.snapshot().lines, ["   A    ", "AA      "])

    def test_origin_mode_addresses_relative_to_vertical_margins(self):
        with Shitty(columns=8, rows=6) as terminal:
            terminal.write(b"\x1b[2;5r\x1b[?6hX\x1b[4;1HY")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(0, 1).char, "X")
            self.assertEqual(snapshot.cell(0, 4).char, "Y")

    def test_origin_mode_addresses_relative_to_both_margin_pairs(self):
        with Shitty(columns=10, rows=6) as terminal:
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

    def test_origin_mode_horizontal_position_commands_use_margins(self):
        for position in (b"\x1b[1G", b"\x1b[1`"):
            with self.subTest(position=position):
                with Shitty(columns=10, rows=6) as terminal:
                    terminal.write(
                        b"\x1b[2;5r"
                        b"\x1b[?69h"
                        b"\x1b[3;8s"
                        b"\x1b[?6h"
                        b"\x1b[2;4H" + position + b"X"
                    )
                    self.assertEqual(terminal.snapshot().cell(2, 2).char, "X")

        with Shitty(columns=10, rows=6) as terminal:
            terminal.write(
                b"\x1b[2;5r"
                b"\x1b[?69h"
                b"\x1b[3;8s"
                b"\x1b[?6h"
                b"\x1b[2;1H"
                b"\x1b[2a"
                b"X"
            )
            self.assertEqual(terminal.snapshot().cell(4, 2).char, "X")

    def test_origin_mode_vertical_position_commands_use_margins(self):
        with Shitty(columns=10, rows=8) as terminal:
            terminal.write(
                b"\x1b[3;6r"
                b"\x1b[?6h"
                b"\x1b[1d"
                b"X"
                b"\x1b[99e"
                b"Y"
            )
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(0, 2).char, "X")
            self.assertEqual(snapshot.cell(1, 5).char, "Y")

    def test_resetting_origin_mode_homes_cursor_absolutely(self):
        with Shitty(columns=8, rows=5) as terminal:
            terminal.write(b"\x1b[2;4r\x1b[?6h\x1b[?6lX")
            self.assertEqual(terminal.snapshot().cell(0, 0).char, "X")

    def test_vt52_cursor_addressing_and_device_attributes(self):
        with Shitty(columns=8, rows=5) as terminal:
            terminal.write(b"\x1b[?2l\x1bY\x22\x24X\x1bZ")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(4, 2).char, "X")
            self.assertEqual(terminal.read_input(), b"\x1b/Z")

    def test_vt52_can_return_to_ansi_mode(self):
        with Shitty(columns=8, rows=5) as terminal:
            terminal.write(b"\x1b[?2l\x1b<\x1b[2;3HX")
            self.assertEqual(terminal.snapshot().cell(2, 1).char, "X")

    def test_vt52_graphics_blank_advances_and_del_is_ignored(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[?2l\x1bF_\x7f\x1bGX")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines[0][:2], " X")
            self.assertEqual(snapshot.cursor_x, 2)

    def test_vt100_compatibility_ignores_decrqss_and_s8c1t(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[?2l\x1b<\x1bP$q\"p\x1b\\")
            self.assertEqual(terminal.read_input(), b"")

            terminal.write(b"\x1b G\x1b[6n")
            self.assertEqual(terminal.read_input(), b"\x1b[1;1R")

    def test_decscl_restores_extended_controls_from_vt100_mode(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(
                b"\x1b[?2l\x1b<"
                b"\x1b[64;1\"p"
                b"\x1bP$q\"p\x1b\\"
            )
            self.assertEqual(terminal.read_input(), b"\x1bP1$r64;1\"p\x1b\\")

    def test_decaln_fills_screen_without_leaking_attributes(self):
        with Shitty(columns=5, rows=3) as terminal:
            terminal.write(b"\x1b[1;31m\x1b#8X")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, ["XEEEE", "EEEEE", "EEEEE"])
            self.assertTrue(snapshot.cell(0, 0).bold)
            self.assertFalse(snapshot.cell(1, 0).bold)

    def test_decaln_homes_cursor_and_resets_both_margin_pairs(self):
        with Shitty(columns=12, rows=8) as terminal:
            terminal.write(
                b"\x1b[?69h\x1b[3;10s\x1b[3;6r"
                b"\x1b[5;6H\x1b#8"
            )
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 0))

            terminal.write(b"\x1b[3;3H\x1b[9A\x1b[9D")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 0))

            terminal.write(b"\x1b[6;10H\x1b[9B\x1b[9C")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (11, 7))

    def test_horizontal_tab_stop_set_and_clear(self):
        with Shitty(columns=12, rows=2) as terminal:
            terminal.write(b"\x1b[3g\x1b[1;4H\x1bH\r\tX\x1b[3g\r\tY")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(3, 0).char, "X")
            self.assertEqual(snapshot.cell(11, 0).char, "Y")

    def test_ris_resets_screen_modes_and_kitty_flags(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(
                b"text\x1b[?7l\x1b[?1003h\x1b[?1006h\x1b[>7u\x1bc"
            )
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, ["        ", "        "])
            self.assertEqual(terminal.state(), (0, 0, 0, 0))

    def test_ris_from_alternate_screen_clears_primary_screen(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"primary\x1b[?1049halt\x1bc")
            snapshot = terminal.snapshot()

            self.assertEqual(snapshot.lines, ["        ", "        "])
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 0))

            terminal.write(b"\x1b[?47h")
            self.assertEqual(
                terminal.snapshot().lines,
                ["        ", "        "],
            )

    def test_decstr_restores_default_margins(self):
        with Shitty(columns=10, rows=6) as terminal:
            terminal.write(
                b"\x1b[2;5r\x1b[?69h\x1b[3;8s\x1b[?6h"
                b"\x1b[!p\x1b[?6hX"
            )
            snapshot = terminal.snapshot()

            self.assertEqual(snapshot.cell(0, 0).char, "X")

    def test_single_margin_parameter_uses_screen_end_default(self):
        with Shitty(columns=10, rows=6) as terminal:
            terminal.write(b"\x1b[3r\x1b[?6hX")
            self.assertEqual(terminal.snapshot().cell(0, 2).char, "X")

            terminal.write(b"\x1b[?6l\x1b[r\x1b[?69h\x1b[4s\x1b[?6hY")
            self.assertEqual(terminal.snapshot().cell(3, 0).char, "Y")

    def test_origin_mode_cpr_is_relative_to_both_margins(self):
        with Shitty(columns=10, rows=6) as terminal:
            terminal.write(
                b"\x1b[2;5r\x1b[?69h\x1b[3;8s\x1b[?6h\x1b[6n"
            )
            self.assertEqual(terminal.read_input(), b"\x1b[1;1R")


if __name__ == "__main__":
    unittest.main()
