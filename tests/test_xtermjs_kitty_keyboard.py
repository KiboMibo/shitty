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
KEY_F3 = 292
KEY_F4 = 293
KEY_F5 = 294
KEY_F6 = 295
KEY_F7 = 296
KEY_F8 = 297
KEY_F9 = 298
KEY_F10 = 299
KEY_F11 = 300
KEY_F12 = 301
KEY_F13 = 302
KEY_F14 = 303
KEY_F20 = 309
KEY_F24 = 313
KEY_KP_0 = 320
KEY_KP_1 = 321
KEY_KP_9 = 329
KEY_KP_DECIMAL = 330

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
    "F3 retains SS3 R encoding",
    "F4 retains SS3 S encoding",
    "F5 retains CSI 15 tilde encoding",
    "F6 retains CSI 17 tilde encoding",
    "F7 retains CSI 18 tilde encoding",
    "F8 retains CSI 19 tilde encoding",
    "F9 retains CSI 20 tilde encoding",
    "F10 retains CSI 21 tilde encoding",
    "F11 retains CSI 23 tilde encoding",
    "F12 retains CSI 24 tilde encoding",
    "shift F1 includes modifier two",
    "control F5 includes modifier five",
    "F13 uses private-use code 57376",
    "F14 uses private-use code 57377",
    "F20 uses private-use code 57383",
    "F24 uses private-use code 57387",
    "numpad zero uses private-use code 57399",
    "numpad one uses private-use code 57400",
    "numpad nine uses private-use code 57408",
    "numpad decimal uses private-use code 57409",
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
    def test_upstream_inventory_has_60_distinct_cases(self):
        self.assertEqual(len(UPSTREAM_CASES), 60)
        self.assertEqual(len(set(UPSTREAM_CASES)), 60)

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

    @unittest.expectedFailure
    def test_f3_retains_ss3_r_encoding(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_F3, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1bOR")

    @unittest.expectedFailure
    def test_f4_retains_ss3_s_encoding(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_F4, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1bOS")

    def test_f5_retains_csi_15_tilde_encoding(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_F5, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1b[15~")

    def test_f6_retains_csi_17_tilde_encoding(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_F6, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1b[17~")

    def test_f7_retains_csi_18_tilde_encoding(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_F7, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1b[18~")

    def test_f8_retains_csi_19_tilde_encoding(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_F8, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1b[19~")

    def test_f9_retains_csi_20_tilde_encoding(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_F9, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1b[20~")

    def test_f10_retains_csi_21_tilde_encoding(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_F10, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1b[21~")

    def test_f11_retains_csi_23_tilde_encoding(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_F11, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1b[23~")

    def test_f12_retains_csi_24_tilde_encoding(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_F12, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1b[24~")

    def test_shift_f1_includes_modifier_two(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_F1, PRESS, modifiers=SHIFT)
            self.assertEqual(terminal.read_input(), b"\x1b[1;2P")

    def test_control_f5_includes_modifier_five(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_F5, PRESS, modifiers=CONTROL)
            self.assertEqual(terminal.read_input(), b"\x1b[15;5~")

    def test_f13_uses_private_use_code_57376(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_F13, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1b[57376u")

    def test_f14_uses_private_use_code_57377(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_F14, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1b[57377u")

    def test_f20_uses_private_use_code_57383(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_F20, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1b[57383u")

    def test_f24_uses_private_use_code_57387(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_F24, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1b[57387u")

    def test_numpad_zero_uses_private_use_code_57399(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_KP_0, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1b[57399u")

    def test_numpad_one_uses_private_use_code_57400(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_KP_1, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1b[57400u")

    def test_numpad_nine_uses_private_use_code_57408(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_KP_9, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1b[57408u")

    def test_numpad_decimal_uses_private_use_code_57409(self):
        with Shitty(columns=8, rows=2) as terminal:
            enable_disambiguation(terminal)
            terminal.frontend_key_event(KEY_KP_DECIMAL, PRESS)
            self.assertEqual(terminal.read_input(), b"\x1b[57409u")


if __name__ == "__main__":
    unittest.main()
