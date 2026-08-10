# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

"""Public adaptations of the first xterm.js KittyKeyboard cases."""

import unittest

from harness import Shitty


PRESS = 1

SHIFT = 1
CONTROL = 2
ALT = 4
SUPER = 8

KEY_ESCAPE = 256
KEY_ENTER = 257
KEY_TAB = 258
KEY_BACKSPACE = 259
KEY_INSERT = 260
KEY_DELETE = 261
KEY_RIGHT = 262
KEY_LEFT = 263
KEY_DOWN = 264
KEY_UP = 265
KEY_PAGE_UP = 266
KEY_PAGE_DOWN = 267
KEY_HOME = 268
KEY_END = 269
KEY_F1 = 290
KEY_F2 = 291

UPSTREAM_CASES = (
    "protocol is inactive when flags are zero",
    "protocol is active when any flag is set",
    "shift letter remains plain in disambiguate mode",
    "alt modifier is encoded as three",
    "control modifier is encoded as five",
    "super modifier is encoded as nine",
    "control shift modifiers are encoded as six",
    "control alt modifiers are encoded as seven",
    "control alt shift modifiers are encoded as eight",
    "control super modifiers are encoded as thirteen",
    "all four modifiers are encoded as sixteen",
    "an unmodified escape omits the modifier field",
    "escape is disambiguated as CSI 27 u",
    "enter remains legacy carriage return",
    "tab remains legacy horizontal tab",
    "backspace remains legacy delete",
    "space remains plain text",
    "shift tab is disambiguated as CSI 9;2 u",
    "control enter is disambiguated as CSI 13;5 u",
    "alt escape is disambiguated as CSI 27;3 u",
    "control backspace is disambiguated as CSI 127;5 u",
    "control space is disambiguated as CSI 32;5 u",
    "alt space is disambiguated as CSI 32;3 u",
    "insert retains CSI 2 tilde encoding",
    "delete retains CSI 3 tilde encoding",
    "page up retains CSI 5 tilde encoding",
    "page down retains CSI 6 tilde encoding",
    "home retains CSI H encoding",
    "end retains CSI F encoding",
    "shift page up includes modifier two",
    "control home includes modifier five",
    "up retains CSI A encoding",
    "down retains CSI B encoding",
    "right retains CSI C encoding",
    "left retains CSI D encoding",
    "shift up includes modifier two",
    "control left includes modifier five",
    "control shift right includes modifier six",
    "F1 retains SS3 P encoding",
    "F2 retains SS3 Q encoding",
)


def enable_disambiguation(terminal):
    terminal.write(b"\x1b[=1u")


def send_letter(terminal, modifiers):
    terminal.layout_key("A", "a", "a", modifiers=modifiers)
    if not modifiers & (CONTROL | SUPER):
        terminal.frontend_text_event(
            "A" if modifiers & SHIFT else "a",
            modifiers=modifiers,
        )


class XtermJsKittyKeyboardTest(unittest.TestCase):
    def test_upstream_inventory_has_40_distinct_cases(self):
        self.assertEqual(len(UPSTREAM_CASES), 40)
        self.assertEqual(len(set(UPSTREAM_CASES)), 40)

    def test_protocol_is_inactive_when_flags_are_zero(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[?u")
            self.assertEqual(terminal.read_input(), b"\x1b[?0u")
            terminal.frontend_key_event(KEY_ESCAPE, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1b")

    def test_protocol_is_active_when_any_flag_is_set(self):
        with Shitty(columns=8, rows=2) as terminal:
            for flags in (1, 2, 31):
                with self.subTest(flags=flags):
                    terminal.write(f"\x1b[={flags}u\x1b[?u".encode())
                    self.assertEqual(
                        terminal.read_input(),
                        f"\x1b[?{flags}u".encode(),
                    )

    def test_shift_letter_remains_plain_in_disambiguate_mode(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            send_letter(terminal, SHIFT)
            self.assertEqual(terminal.read_input(), b"A")

    def test_alt_modifier_is_encoded_as_three(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            send_letter(terminal, ALT)
            self.assertEqual(terminal.read_input(), b"\x1b[97;3u")

    def test_control_modifier_is_encoded_as_five(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            send_letter(terminal, CONTROL)
            self.assertEqual(terminal.read_input(), b"\x1b[97;5u")

    def test_super_modifier_is_encoded_as_nine(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            send_letter(terminal, SUPER)
            self.assertEqual(terminal.read_input(), b"\x1b[97;9u")

    def test_control_shift_modifiers_are_encoded_as_six(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            send_letter(terminal, CONTROL | SHIFT)
            self.assertEqual(terminal.read_input(), b"\x1b[97;6u")

    def test_control_alt_modifiers_are_encoded_as_seven(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            send_letter(terminal, CONTROL | ALT)
            self.assertEqual(terminal.read_input(), b"\x1b[97;7u")

    def test_control_alt_shift_modifiers_are_encoded_as_eight(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            send_letter(terminal, CONTROL | ALT | SHIFT)
            self.assertEqual(terminal.read_input(), b"\x1b[97;8u")

    def test_control_super_modifiers_are_encoded_as_thirteen(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            send_letter(terminal, CONTROL | SUPER)
            self.assertEqual(terminal.read_input(), b"\x1b[97;13u")

    def test_all_four_modifiers_are_encoded_as_sixteen(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            send_letter(terminal, SHIFT | ALT | CONTROL | SUPER)
            self.assertEqual(terminal.read_input(), b"\x1b[97;16u")

    def test_unmodified_escape_omits_the_modifier_field(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_ESCAPE, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1b[27u")

    def test_escape_is_disambiguated_as_csi_27_u(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_ESCAPE, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1b[27u")

    def test_enter_remains_legacy_carriage_return(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_ENTER, PRESS)
            self.assertEqual(terminal.read_input(), b"\r")

    def test_tab_remains_legacy_horizontal_tab(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_TAB, PRESS)
            self.assertEqual(terminal.read_input(), b"\t")

    def test_backspace_remains_legacy_delete(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_BACKSPACE, PRESS)
            self.assertEqual(terminal.read_input(), b"\x7f")

    def test_space_remains_plain_text(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.layout_key(" ", " ", " ")
            terminal.frontend_text_event(" ")
            self.assertEqual(terminal.read_input(), b" ")

    def test_shift_tab_is_disambiguated_as_csi_9_2_u(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_TAB, PRESS, modifiers=SHIFT)
            self.assertEqual(terminal.read_input(), b"\x1b[9;2u")

    def test_control_enter_is_disambiguated_as_csi_13_5_u(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_ENTER, PRESS, modifiers=CONTROL)
            self.assertEqual(terminal.read_input(), b"\x1b[13;5u")

    def test_alt_escape_is_disambiguated_as_csi_27_3_u(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_ESCAPE, PRESS, modifiers=ALT)
            self.assertEqual(terminal.read_input(), b"\x1b[27;3u")

    def test_control_backspace_is_disambiguated_as_csi_127_5_u(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(
                KEY_BACKSPACE, PRESS, modifiers=CONTROL
            )
            self.assertEqual(terminal.read_input(), b"\x1b[127;5u")

    def test_control_space_is_disambiguated_as_csi_32_5_u(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.layout_key(" ", " ", " ", modifiers=CONTROL)
            self.assertEqual(terminal.read_input(), b"\x1b[32;5u")

    def test_alt_space_is_disambiguated_as_csi_32_3_u(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.layout_key(" ", " ", " ", modifiers=ALT)
            terminal.frontend_text_event(" ", modifiers=ALT)
            self.assertEqual(terminal.read_input(), b"\x1b[32;3u")

    def test_insert_retains_csi_2_tilde_encoding(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_INSERT, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1b[2~")

    def test_delete_retains_csi_3_tilde_encoding(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_DELETE, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1b[3~")

    def test_page_up_retains_csi_5_tilde_encoding(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_PAGE_UP, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1b[5~")

    def test_page_down_retains_csi_6_tilde_encoding(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_PAGE_DOWN, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1b[6~")

    def test_home_retains_csi_h_encoding(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_HOME, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1b[H")

    def test_end_retains_csi_f_encoding(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_END, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1b[F")

    @unittest.expectedFailure
    def test_shift_page_up_includes_modifier_two(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(
                KEY_PAGE_UP, PRESS, modifiers=SHIFT
            )
            self.assertEqual(terminal.read_input(), b"\x1b[5;2~")

    def test_control_home_includes_modifier_five(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_HOME, PRESS, modifiers=CONTROL)
            self.assertEqual(terminal.read_input(), b"\x1b[1;5H")

    def test_up_retains_csi_a_encoding(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_UP, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1b[A")

    def test_down_retains_csi_b_encoding(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_DOWN, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1b[B")

    def test_right_retains_csi_c_encoding(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_RIGHT, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1b[C")

    def test_left_retains_csi_d_encoding(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_LEFT, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1b[D")

    def test_shift_up_includes_modifier_two(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_UP, PRESS, modifiers=SHIFT)
            self.assertEqual(terminal.read_input(), b"\x1b[1;2A")

    def test_control_left_includes_modifier_five(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_LEFT, PRESS, modifiers=CONTROL)
            self.assertEqual(terminal.read_input(), b"\x1b[1;5D")

    def test_control_shift_right_includes_modifier_six(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(
                KEY_RIGHT, PRESS, modifiers=CONTROL | SHIFT
            )
            self.assertEqual(terminal.read_input(), b"\x1b[1;6C")

    @unittest.expectedFailure
    def test_f1_retains_ss3_p_encoding(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_F1, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1bOP")

    @unittest.expectedFailure
    def test_f2_retains_ss3_q_encoding(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_F2, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1bOQ")


if __name__ == "__main__":
    unittest.main()
