import unittest

from harness import Zutty


class KeyboardTest(unittest.TestCase):
    def test_cursor_key_normal_and_application_modes(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.key("UP")
            terminal.write(b"\x1b[?1h")
            terminal.key("UP")
            self.assertEqual(terminal.read_input(), b"\x1b[A\x1bOA")

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

    def test_kitty_key_flags_control_optional_fields(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[>7u")
            self.assertEqual(terminal.state()[3], 7)
            terminal.kitty_key(ord("a"), shifted=ord("A"), modifiers=1)
            self.assertEqual(terminal.read_input(), b"\x1b[97:65;2:1u")

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


if __name__ == "__main__":
    unittest.main()
