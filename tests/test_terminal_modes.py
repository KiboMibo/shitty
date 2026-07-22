import unittest

from harness import Zutty


class TerminalModeTest(unittest.TestCase):
    def test_deccolm_resizes_and_clears_the_terminal_page(self):
        with Zutty(columns=80, rows=24) as terminal:
            terminal.write(b"content\x1b[?3h")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.columns, snapshot.rows), (132, 24))
            self.assertEqual(snapshot.lines[0], " " * 132)
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 0))

            terminal.write(b"content\x1b[?3l")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.columns, snapshot.rows), (80, 24))
            self.assertEqual(snapshot.lines[0], " " * 80)
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 0))

    def test_conformance_state_exposes_screen_and_mode_vector(self):
        with Zutty() as terminal:
            self.assertEqual(terminal.conformance_state(), {
                "screen": "Primary",
                "IRM": False,
                "SRM": True,
                "LNM": False,
                "DECCKM": False,
                "DECCOLM": False,
                "DECSCLM": False,
                "DECSCNM": False,
                "DECOM": False,
                "DECAWM": True,
                "DECARM": False,
                "DECTCEM": True,
                "DECNKM": False,
                "DECBKM": False,
                "DECLRMM": False,
            })

            terminal.write(
                b"\x1b[4;20h"
                b"\x1b[?1;3;4;5;6;8;67;69h"
                b"\x1b="
                b"\x1b[?47h"
            )
            state = terminal.conformance_state()
            self.assertEqual(state["screen"], "Alternate")
            for mode in (
                "IRM", "LNM", "DECCKM", "DECCOLM", "DECSCLM", "DECSCNM",
                "DECOM", "DECARM", "DECNKM", "DECBKM", "DECLRMM",
            ):
                self.assertTrue(state[mode], mode)

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

    def test_decnkm_controls_and_reports_numeric_keypad_mode(self):
        with Zutty() as terminal:
            terminal.write(b"\x1b[?66$p")
            terminal.key("KP_1")
            self.assertEqual(terminal.read_input(), b"\x1b[?66;2$y1")

            terminal.write(b"\x1b[?66h\x1b[?66$p")
            terminal.key("KP_1")
            self.assertEqual(
                terminal.read_input(), b"\x1b[?66;1$y\x1bOq"
            )

            terminal.write(b"\x1b[?66l\x1b[?66$p")
            terminal.key("KP_1")
            self.assertEqual(terminal.read_input(), b"\x1b[?66;2$y1")

    def test_decscl_selects_protocol_levels_and_reports_vt420_up(self):
        with Zutty() as terminal:
            terminal.write(b"\x1b[61;1\"p\x1b[4$p")
            self.assertEqual(terminal.read_input(), b"")

            terminal.write(b"\x1b[62;1\"p\x1b[4$p")
            self.assertEqual(terminal.read_input(), b"")

            terminal.write(b"\x1b[63;1\"p\x1b[4$p")
            self.assertEqual(terminal.read_input(), b"\x1b[4;2$y")

            for level in (64, 65):
                with self.subTest(level=level):
                    terminal.write(
                        f"\x1b[{level};1\"p\x1bP$q\"p\x1b\\".encode()
                    )
                    self.assertEqual(
                        terminal.read_input(),
                        f"\x1bP1$r{level};1\"p\x1b\\".encode(),
                    )

    def test_decscl_hard_resets_terminal_before_selecting_level(self):
        with Zutty() as terminal:
            terminal.write(
                b"text\x1b[6;7H\x1b7\x1b[4h"
                b"\x1b[61\"p\x1b8X"
            )
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (1, 0))
            self.assertEqual(snapshot.lines[0][:2], "X ")

    def test_decscl_gates_level_specific_sequences(self):
        with Zutty(columns=10, rows=4) as terminal:
            terminal.write(
                b"\x1b[63;1\"p\x1b[?69h\x1b[3;5s\x1b[1;5Habc"
            )
            self.assertEqual(terminal.snapshot().cursor_x, 7)

            terminal.write(
                b"\x1b[64;1\"p\x1b[?69h\x1b[3;5s\x1b[1;3Habc"
            )
            self.assertEqual(terminal.snapshot().cursor_x, 4)

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
