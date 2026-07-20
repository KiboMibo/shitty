import unittest

from harness import Zutty


class MouseFrontendPointerTest(unittest.TestCase):
    def test_vt200_reports_left_middle_right_press_and_release(self):
        with Zutty(columns=8, rows=4) as terminal:
            terminal.write(b"\x1b[?1000h\x1b[?1006h")
            for button, code in ((0, 0), (2, 1), (1, 2)):
                terminal.button(button, True, x=5, y=4)
                terminal.button(button, False, x=5, y=4)
                self.assertEqual(
                    terminal.read_input(),
                    f"\x1b[<{code};3;2M\x1b[<{code};3;2m".encode(),
                )

    def test_x10_reports_only_press_and_strips_modifiers(self):
        with Zutty(columns=8, rows=4) as terminal:
            terminal.write(b"\x1b[?9h\x1b[?1006h")
            terminal.button(0, True, modifiers=6, x=5, y=4)
            terminal.button(0, False, modifiers=6, x=5, y=4)
            self.assertEqual(terminal.read_input(), b"\x1b[<0;3;2M")

    def test_extended_buttons_have_bounded_press_only_protocol(self):
        with Zutty(columns=8, rows=4) as terminal:
            terminal.write(b"\x1b[?1000h\x1b[?1006h")
            for button in range(3, 8):
                terminal.button(button, True)
                terminal.button(button, False)
            self.assertEqual(
                terminal.read_input(),
                b"\x1b[<128;1;1M\x1b[<129;1;1M"
                b"\x1b[<130;1;1M\x1b[<131;1;1M",
            )

    def test_button_reporting_encodes_all_modifiers(self):
        with Zutty(columns=8, rows=4) as terminal:
            terminal.write(b"\x1b[?1000h\x1b[?1006h")
            terminal.button(0, True, modifiers=6, x=5, y=4)
            self.assertEqual(terminal.read_input(), b"\x1b[<24;3;2M")

    def test_button_event_motion_requires_a_primary_button_and_deduplicates(self):
        with Zutty(columns=8, rows=4) as terminal:
            terminal.write(b"\x1b[?1002h\x1b[?1006h")
            terminal.pointer(5, 4)
            self.assertEqual(terminal.read_input(), b"")
            terminal.button(0, True, x=5, y=4)
            terminal.pointer(5, 4)
            terminal.pointer(5.4, 4.4)
            terminal.pointer(6, 4)
            terminal.button(0, False, x=6, y=4)
            self.assertEqual(
                terminal.read_input(),
                b"\x1b[<0;3;2M\x1b[<32;3;2M"
                b"\x1b[<32;4;2M\x1b[<0;4;2m",
            )

    def test_any_event_motion_reports_without_buttons_and_with_modifiers(self):
        with Zutty(columns=8, rows=4) as terminal:
            terminal.write(b"\x1b[?1003h\x1b[?1006h")
            terminal.pointer(5, 4, modifiers=6)
            terminal.pointer(5, 4, modifiers=6)
            self.assertEqual(terminal.read_input(), b"\x1b[<59;3;2M")

    def test_tracking_mode_transition_resets_motion_deduplication(self):
        with Zutty(columns=8, rows=4) as terminal:
            terminal.write(b"\x1b[?1003h\x1b[?1006h")
            terminal.pointer(5, 4)
            terminal.write(b"\x1b[?1003l\x1b[?1003h")
            terminal.pointer(5.4, 4.4)
            self.assertEqual(
                terminal.read_input(),
                b"\x1b[<35;3;2M\x1b[<35;3;2M",
            )

    def test_sgr_pixel_motion_deduplicates_pixels_not_cells(self):
        with Zutty(columns=8, rows=4) as terminal:
            terminal.write(b"\x1b[?1003h\x1b[?1016h")
            terminal.pointer(5, 4)
            terminal.pointer(5.4, 4)
            terminal.pointer(6, 4)
            self.assertEqual(
                terminal.read_input(),
                b"\x1b[<35;4;3M\x1b[<35;5;3M",
            )

    def test_content_scale_converts_logical_pointer_to_framebuffer_pixels(self):
        with Zutty(columns=16, rows=8) as terminal:
            terminal.write(b"\x1b[?1003h\x1b[?1016h")
            terminal.pointer(3, 2, scale_x=2, scale_y=2)
            self.assertEqual(terminal.read_input(), b"\x1b[<35;5;3M")

    def test_shift_override_drags_a_local_selection_during_mouse_reporting(self):
        with Zutty(columns=8, rows=4) as terminal:
            terminal.write(b"one two\x1b[?1000h\x1b[?1006h")
            terminal.button(0, True, x=2, y=2, modifiers=1)
            terminal.pointer(5, 2, modifiers=1)
            selected = terminal.button(0, False, x=5, y=2, modifiers=1)
            self.assertEqual(selected, b"one")
            self.assertEqual(terminal.read_input(), b"")

    def test_double_and_triple_click_cycle_word_and_line_selection(self):
        with Zutty(columns=12, rows=3) as terminal:
            terminal.write(b"one two")
            terminal.button(0, True, x=6, y=2, time=1.0)
            self.assertEqual(
                terminal.button(0, False, x=6, y=2, time=1.01), b""
            )
            terminal.button(0, True, x=6, y=2, time=1.1)
            self.assertEqual(
                terminal.button(0, False, x=6, y=2, time=1.11), b"two"
            )
            terminal.button(0, True, x=6, y=2, time=1.2)
            self.assertEqual(
                terminal.button(0, False, x=6, y=2, time=1.21), b"one two"
            )
            terminal.button(0, True, x=6, y=2, time=1.3)
            self.assertEqual(
                terminal.button(0, False, x=6, y=2, time=1.31), b""
            )

    def test_click_timeout_and_distance_restart_at_character_selection(self):
        with Zutty(columns=12, rows=3) as terminal:
            terminal.write(b"one two")
            terminal.button(0, True, x=6, y=2, time=1.0)
            terminal.button(0, False, x=6, y=2, time=1.01)
            terminal.button(0, True, x=6, y=2, time=1.6)
            self.assertEqual(
                terminal.button(0, False, x=6, y=2, time=1.61), b""
            )
            terminal.button(0, True, x=1, y=2, time=1.7)
            self.assertEqual(
                terminal.button(0, False, x=1, y=2, time=1.71), b""
            )

    def test_click_thresholds_are_inclusive_and_clock_rewind_resets(self):
        with Zutty(columns=12, rows=3) as terminal:
            terminal.write(b"one two")
            terminal.button(0, True, x=6, y=2, time=1)
            terminal.button(0, False, x=6, y=2, time=1.01)
            terminal.button(0, True, x=2, y=2, time=1.5)
            self.assertEqual(
                terminal.button(0, False, x=2, y=2, time=1.51), b"one two"
            )
            terminal.button(0, True, x=2, y=2, time=1)
            self.assertEqual(
                terminal.button(0, False, x=2, y=2, time=1.01), b""
            )

    def test_pointer_updates_dec_locator_position(self):
        with Zutty(columns=8, rows=4) as terminal:
            terminal.write(b"\x1b[1;2'z")
            terminal.pointer(5, 4)
            terminal.write(b"\x1b['|")
            self.assertEqual(terminal.read_input(), b"\x1b[1;0;2;3;0&w")

    def test_highlight_release_uses_frontend_button_coordinates(self):
        with Zutty(columns=8, rows=4) as terminal:
            terminal.write(
                b"\x1b[?1001h\x1b[?1006h\x1b[1;2;1;1;4T"
            )
            terminal.button(0, True, x=5, y=4)
            terminal.button(0, False, x=5, y=4)
            self.assertEqual(
                terminal.read_input(),
                b"\x1b[<0;3;2M\x1b[<2;1;3;2;3;2T",
            )

    def test_right_button_extends_selection_and_middle_button_pastes_it(self):
        with Zutty(columns=8, rows=3) as terminal:
            terminal.write(b"abcd")
            terminal.button(0, True, x=2, y=2, time=1)
            terminal.button(0, False, x=2, y=2, time=1.01)
            terminal.button(1, True, x=5, y=2, time=2)
            self.assertEqual(
                terminal.button(1, False, x=5, y=2, time=2.01), b"abc"
            )
            terminal.button(2, True, x=4, y=2, time=3)
            terminal.button(2, False, x=4, y=2, time=3.01)
            self.assertEqual(terminal.read_input(), b"abc")


if __name__ == "__main__":
    unittest.main()
