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
    def test_upstream_inventory_has_20_distinct_cases(self):
        self.assertEqual(len(UPSTREAM_CASES), 20)
        self.assertEqual(len(set(UPSTREAM_CASES)), 20)

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


if __name__ == "__main__":
    unittest.main()
