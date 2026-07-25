# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import unittest

from harness import Shitty


GLFW_RELEASE = 0
GLFW_PRESS = 1
GLFW_KEY_0 = 48
GLFW_KEY_A = 65
GLFW_KEY_EQUAL = 61
GLFW_KEY_MINUS = 45
GLFW_MOD_SHIFT = 1
GLFW_MOD_CONTROL = 2


class FontResizeTest(unittest.TestCase):
    @staticmethod
    def shortcut(terminal, key, text, modifiers):
        terminal.frontend_key_event(key, GLFW_PRESS, modifiers=modifiers)
        terminal.frontend_text_event(text, modifiers=modifiers)
        terminal.frontend_key_event(key, GLFW_RELEASE, modifiers=modifiers)

    def test_font_bindings_resize_and_reset_without_reaching_child(self):
        with Shitty(
            columns=40,
            rows=8,
            save_lines=8,
            glyph_px=8,
            glyph_py=16,
            extra_arguments=("-fontsize", "16"),
        ) as terminal:
            terminal.write(b"one\r\ntwo\r\nthree\r\nfour\r\nfive\r\nsix")
            terminal.wheel_up(1)
            terminal.select_start(0, 0)
            terminal.select_update(3, 0)
            before = terminal.snapshot()
            initial = terminal.font_state()
            horizontal_padding = initial[3] - initial[5] * initial[1]
            vertical_padding = initial[4] - initial[6] * initial[2]

            self.shortcut(
                terminal,
                GLFW_KEY_EQUAL,
                "+",
                GLFW_MOD_CONTROL | GLFW_MOD_SHIFT,
            )
            increased = terminal.font_state()

            self.assertEqual(increased[0], 17)
            self.assertEqual(increased[5:7], initial[5:7])
            self.assertEqual(
                increased[3],
                horizontal_padding + increased[5] * increased[1],
            )
            self.assertEqual(
                increased[4],
                vertical_padding + increased[6] * increased[2],
            )
            self.assertNotEqual(increased[3:5], initial[3:5])
            self.assertEqual(terminal.winsize(), increased[5:7])
            self.assertEqual(terminal.read_input(), b"")
            self.assertGreater(terminal.snapshot().refresh_count, before.refresh_count)
            self.assertEqual(terminal.select_finish(), b"one")

            self.shortcut(
                terminal,
                GLFW_KEY_MINUS,
                "-",
                GLFW_MOD_CONTROL,
            )
            self.assertEqual(terminal.font_state()[0], 16)
            self.shortcut(
                terminal,
                GLFW_KEY_EQUAL,
                "+",
                GLFW_MOD_CONTROL | GLFW_MOD_SHIFT,
            )
            self.shortcut(
                terminal,
                GLFW_KEY_EQUAL,
                "+",
                GLFW_MOD_CONTROL | GLFW_MOD_SHIFT,
            )
            self.assertEqual(terminal.font_state()[0], 18)
            self.shortcut(
                terminal,
                GLFW_KEY_0,
                "0",
                GLFW_MOD_CONTROL,
            )
            self.assertEqual(terminal.font_state()[0], 16)
            self.assertEqual(terminal.read_input(), b"")

            terminal.frontend_key_event(
                GLFW_KEY_A,
                GLFW_PRESS,
                modifiers=GLFW_MOD_CONTROL,
            )
            terminal.frontend_key_event(
                GLFW_KEY_A,
                GLFW_RELEASE,
                modifiers=GLFW_MOD_CONTROL,
            )
            self.assertEqual(terminal.read_input(), b"\x01")


if __name__ == "__main__":
    unittest.main()
