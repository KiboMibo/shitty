# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import unittest

from harness import Shitty


GLFW_RELEASE = 0
GLFW_PRESS = 1
GLFW_KEY_EQUAL = 61
GLFW_KEY_MINUS = 45
GLFW_MOD_SHIFT = 1
GLFW_MOD_CONTROL = 2


class RendererReplacementTest(unittest.TestCase):
    @staticmethod
    def shortcut(terminal, key, text, modifiers):
        terminal.frontend_key_event(key, GLFW_PRESS, modifiers=modifiers)
        terminal.frontend_text_event(text, modifiers=modifiers)
        terminal.frontend_key_event(key, GLFW_RELEASE, modifiers=modifiers)

    def test_failed_font_replacement_preserves_visible_generation(self):
        with Shitty(
            columns=20,
            rows=4,
            glyph_px=8,
            glyph_py=16,
            extra_arguments=("-fontsize", "16"),
        ) as terminal:
            self.shortcut(
                terminal,
                GLFW_KEY_EQUAL,
                "+",
                GLFW_MOD_CONTROL | GLFW_MOD_SHIFT,
            )
            self.shortcut(
                terminal,
                GLFW_KEY_MINUS,
                "-",
                GLFW_MOD_CONTROL,
            )
            terminal.write(b"before")
            before_state = terminal.font_state()
            before_frame = terminal.snapshot()

            terminal.fail_next_font_change()
            terminal.frontend_key_event(
                GLFW_KEY_EQUAL,
                GLFW_PRESS,
                modifiers=GLFW_MOD_CONTROL | GLFW_MOD_SHIFT,
            )
            terminal.frontend_key_event(
                GLFW_KEY_EQUAL,
                GLFW_RELEASE,
                modifiers=GLFW_MOD_CONTROL | GLFW_MOD_SHIFT,
            )

            self.assertEqual(terminal.font_state(), before_state)
            self.assertEqual(terminal.snapshot(), before_frame)
            terminal.write(b"+usable")
            self.assertIn("before+usable", terminal.snapshot().lines[0])

            self.shortcut(
                terminal,
                GLFW_KEY_EQUAL,
                "+",
                GLFW_MOD_CONTROL | GLFW_MOD_SHIFT,
            )
            after_state = terminal.font_state()
            self.assertEqual(after_state[0], 17)
            self.assertNotEqual(after_state[1:5], before_state[1:5])

    def test_failed_output_resize_preserves_previous_frame(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"previous")
            previous = terminal.snapshot()

            terminal.fail_next_present()
            terminal.resize(5, 3)
            self.assertEqual(terminal.snapshot(), previous)

            terminal.write(b"+usable")
            resized = terminal.snapshot()
            self.assertEqual((resized.columns, resized.rows), (5, 3))
            self.assertGreater(resized.refresh_count, previous.refresh_count)

            state = terminal.font_state()
            before_unchanged = terminal.snapshot()
            terminal.resize_pixels(state[3], state[4])
            self.assertEqual(terminal.snapshot(), before_unchanged)
            terminal.write(b"+")
            self.assertGreater(
                terminal.snapshot().refresh_count,
                before_unchanged.refresh_count,
            )


if __name__ == "__main__":
    unittest.main()
