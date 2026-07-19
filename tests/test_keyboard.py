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


if __name__ == "__main__":
    unittest.main()
