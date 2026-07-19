import unittest

from harness import Zutty


class KeyboardTest(unittest.TestCase):
    def test_large_paste_is_queued_without_blocking_control_loop(self):
        payload = b"0123456789abcdef" * 16384
        with Zutty(columns=8, rows=2) as terminal:
            terminal.paste(payload)
            self.assertGreater(terminal.pending_output(), 0)

            received = bytearray()
            while terminal.pending_output():
                received.extend(terminal.read_input())
                terminal.flush_output()
            received.extend(terminal.read_input())
            self.assertEqual(received, payload)

    def test_cursor_key_normal_and_application_modes(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.key("UP")
            terminal.write(b"\x1b[?1h")
            terminal.key("UP")
            self.assertEqual(terminal.read_input(), b"\x1b[A\x1bOA")

    def test_application_keypad_encodes_numeric_key_aliases(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b=")
            terminal.key("KP_0")
            terminal.key("KP_1")
            terminal.key("KP_5")
            terminal.key("KP_9")
            terminal.key("KP_DOT")
            self.assertEqual(
                terminal.read_input(),
                b"\x1bOp\x1bOq\x1bOu\x1bOy\x1bOn",
            )

    def test_vt52_application_keypad(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[?2l\x1b=")
            terminal.key("KP_1")
            terminal.key("KP_PLUS")
            self.assertEqual(terminal.read_input(), b"\x1b?q\x1b?k")

    def test_function_key_and_modified_arrow(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.key("F5")
            terminal.key("LEFT", modifiers=1)
            self.assertEqual(terminal.read_input(), b"\x1b[15~\x1b[1;2D")

    def test_alt_and_control_character_encoding(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.char("a", modifiers=4)
            terminal.char("a", modifiers=2)
            self.assertEqual(terminal.read_input(), b"\x1ba\x1b[27;5;97~")

    def test_bracketed_paste_normalizes_newlines(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[?2004h")
            terminal.paste(b"one\ntwo")
            self.assertEqual(
                terminal.read_input(),
                b"\x1b[200~one\rtwo\x1b[201~",
            )

    def test_plain_paste_normalizes_newlines_without_markers(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.paste(b"one\ntwo")
            self.assertEqual(terminal.read_input(), b"one\rtwo")

    def test_keyboard_lock_discards_user_input(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[2h")
            terminal.char("x")
            terminal.key("UP")
            self.assertEqual(terminal.read_input(), b"")
            terminal.write(b"\x1b[2l")
            terminal.char("x")
            self.assertEqual(terminal.read_input(), b"x")

    def test_local_echo_renders_control_notation(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[12l")
            terminal.char(3)
            self.assertEqual(terminal.read_input(), b"\x03")
            self.assertEqual(terminal.snapshot().lines[0][:2], "^C")

    def test_newline_mode_appends_line_feed_to_return(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[20h")
            terminal.key("RETURN")
            self.assertEqual(terminal.read_input(), b"\r\n")

    def test_backarrow_key_mode_switches_backspace_byte(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.key("BACKSPACE")
            terminal.write(b"\x1b[?67h")
            terminal.key("BACKSPACE")
            self.assertEqual(terminal.read_input(), b"\x7f\x08")

    def test_kitty_key_flags_control_optional_fields(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[>7u")
            self.assertEqual(terminal.state()[3], 7)
            terminal.kitty_key(ord("a"), shifted=ord("A"), modifiers=1)
            self.assertEqual(terminal.read_input(), b"\x1b[97:65;2:1u")

    def test_kitty_supports_all_defined_enhancement_flags(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[>31u\x1b[?u")
            self.assertEqual(terminal.state()[3], 31)
            self.assertEqual(terminal.read_input(), b"\x1b[?31u")

    def test_kitty_associated_text_is_reported_for_text_keys(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[>28u")
            terminal.kitty_key(ord("a"))
            terminal.kitty_key(ord("a"), shifted=ord("A"), modifiers=1)
            self.assertEqual(
                terminal.read_input(),
                b"\x1b[97;1;97u\x1b[97:65;2;65u",
            )

    def test_kitty_reports_modifier_keys_with_all_keys_flag(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[>10u")
            terminal.kitty_special("LEFT_SHIFT", event=1)
            terminal.kitty_special("LEFT_SHIFT", event=3)
            self.assertEqual(
                terminal.read_input(),
                b"\x1b[57441;1:1u\x1b[57441;1:3u",
            )

    def test_kitty_keyboard_stack_is_screen_local(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[>3u")
            self.assertEqual(terminal.state()[3], 3)
            terminal.write(b"\x1b[?1049h")
            self.assertEqual(terminal.state()[3], 0)
            terminal.write(b"\x1b[>5u")
            self.assertEqual(terminal.state()[3], 5)
            terminal.write(b"\x1b[?1049l")
            self.assertEqual(terminal.state()[3], 3)

    def test_kitty_push_pop_set_and_query(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(
                b"\x1b[>1u"
                b"\x1b[=6;2u"
                b"\x1b[?u"
                b"\x1b[=2;3u"
                b"\x1b[?u"
                b"\x1b[<u"
                b"\x1b[?u"
            )
            self.assertEqual(
                terminal.read_input(),
                b"\x1b[?7u\x1b[?5u\x1b[?0u",
            )

    def test_kitty_functional_key_encodings(self):
        expected = {
            "F1": b"\x1b[1;1P",
            "F2": b"\x1b[1;1Q",
            "F3": b"\x1b[1;1R",
            "F4": b"\x1b[1;1S",
            "F5": b"\x1b[15;1~",
            "F13": b"\x1b[57376;1u",
            "KP_0": b"\x1b[57399;1u",
            "KP_ENTER": b"\x1b[57414;1u",
            "CAPS_LOCK": b"\x1b[57358;1u",
            "MENU": b"\x1b[57363;1u",
        }
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[>1u")
            for name, encoded in expected.items():
                with self.subTest(name=name):
                    terminal.kitty_special(name)
                    self.assertEqual(terminal.read_input(), encoded)

    def test_kitty_disambiguates_enter_tab_and_backspace(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[>1u")
            terminal.kitty_special("RETURN")
            terminal.kitty_special("TAB")
            terminal.kitty_special("BACKSPACE")
            self.assertEqual(
                terminal.read_input(),
                b"\x1b[13;1u\x1b[9;1u\x1b[127;1u",
            )

    def test_kitty_event_types_include_release_when_requested(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[>2u")
            terminal.kitty_special("UP", modifiers=5, event=1)
            terminal.kitty_special("UP", modifiers=5, event=2)
            terminal.kitty_special("UP", modifiers=5, event=3)
            self.assertEqual(
                terminal.read_input(),
                b"\x1b[1;6:1A\x1b[1;6:2A\x1b[1;6:3A",
            )

    def test_modify_other_keys_levels(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[>4;0m")
            terminal.char("a", modifiers=1)
            self.assertEqual(terminal.read_input(), b"a")
            terminal.write(b"\x1b[>4;2m")
            terminal.char("a", modifiers=1)
            self.assertEqual(terminal.read_input(), b"\x1b[27;2;97~")

    def test_xtmodkeys_sets_queries_and_resets_every_resource(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(
                b"\x1b[>0;4m\x1b[>1;0m\x1b[>2;0m"
                b"\x1b[>3;4m\x1b[>4;2m\x1b[>6;3m\x1b[>7;1m"
                b"\x1b[?0m\x1b[?1m\x1b[?2m\x1b[?3m"
                b"\x1b[?4m\x1b[?6m\x1b[?7m"
            )
            self.assertEqual(
                terminal.read_input(),
                b"\x1b[>0;4m\x1b[>1;0m\x1b[>2;0m"
                b"\x1b[>3;4m\x1b[>4;2m\x1b[>6;3m\x1b[>7;1m",
            )
            terminal.key("LEFT", modifiers=1)
            terminal.key("F5", modifiers=4)
            self.assertEqual(terminal.read_input(), b"\x1b[D\x1b[15~")

            terminal.write(b"\x1b[>1m\x1b[?1m\x1b[>m\x1b[?4m")
            self.assertEqual(
                terminal.read_input(), b"\x1b[>1;2m\x1b[>4;1m"
            )


if __name__ == "__main__":
    unittest.main()
