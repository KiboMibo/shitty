import unittest

from harness import Zutty


GLFW_RELEASE = 0
GLFW_PRESS = 1
GLFW_REPEAT = 2
GLFW_KEY_A = 65
GLFW_KEY_UP = 265


class KeyboardRepeatTest(unittest.TestCase):
    def test_decarm_controls_printable_and_special_key_repeats(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[?8l")

            terminal.frontend_key_event(GLFW_KEY_A, GLFW_PRESS)
            terminal.frontend_text_event("a")
            terminal.frontend_key_event(GLFW_KEY_A, GLFW_REPEAT)
            terminal.frontend_text_event("a")
            terminal.frontend_text_event("b")
            terminal.frontend_key_event(GLFW_KEY_UP, GLFW_REPEAT)
            self.assertEqual(terminal.read_input(), b"a")

            terminal.write(b"\x1b[?8h")
            terminal.frontend_key_event(GLFW_KEY_A, GLFW_REPEAT)
            terminal.frontend_text_event("a")
            terminal.frontend_key_event(GLFW_KEY_UP, GLFW_REPEAT)
            self.assertEqual(terminal.read_input(), b"a\x1b[A")

    def test_nonrepeat_event_ends_text_suppression(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[?8l")
            terminal.frontend_key_event(GLFW_KEY_A, GLFW_REPEAT)
            terminal.frontend_text_event("a")
            terminal.frontend_key_event(GLFW_KEY_A, GLFW_RELEASE)
            terminal.frontend_key_event(GLFW_KEY_A, GLFW_PRESS)
            terminal.frontend_text_event("a")
            self.assertEqual(terminal.read_input(), b"a")

    def test_decarm_precedes_kitty_repeat_encoding(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[>2u\x1b[?8l")
            terminal.frontend_key_event(GLFW_KEY_UP, GLFW_REPEAT)
            self.assertEqual(terminal.read_input(), b"")

            terminal.write(b"\x1b[?8h")
            terminal.frontend_key_event(GLFW_KEY_UP, GLFW_REPEAT)
            self.assertEqual(terminal.read_input(), b"\x1b[1;1:2A")
