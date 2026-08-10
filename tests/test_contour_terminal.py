# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import unittest

from harness import Shitty


UPSTREAM_CASES = (
    "Terminal.BlinkingCursor",
    "Terminal.IME.CursorVisibleDuringComposition",
    "Terminal.ModifierKeysDoNotScrollViewport",
)


class ContourTerminalTest(unittest.TestCase):
    def test_upstream_inventory_has_first_3_cases(self):
        self.assertEqual(len(UPSTREAM_CASES), 3)
        self.assertEqual(len(set(UPSTREAM_CASES)), 3)

    def test_blinking_cursor_advances_through_both_phases(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[1 q")
            self.assertTrue(terminal.render_state().cursor_blink)
            self.assertTrue(terminal.render_state().blink_visible)

            terminal.blink_tick()
            self.assertFalse(terminal.render_state().blink_visible)
            terminal.blink_tick()
            self.assertTrue(terminal.render_state().blink_visible)

    def test_ime_composition_renders_while_blink_phase_is_off(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[1 q")
            terminal.blink_tick()
            self.assertFalse(terminal.render_state().blink_visible)

            terminal.preedit("test", 0, 4)
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines[0], "test    ")
            self.assertEqual(
                [snapshot.cell(column, 0).inverse for column in range(4)],
                [True] * 4,
            )

    def test_reported_modifier_keys_preserve_the_scrolled_viewport(self):
        modifiers = (
            (280, 16),
            (282, 32),
            (340, 1),
            (341, 2),
            (342, 4),
            (343, 8),
            (344, 1),
            (345, 2),
            (346, 4),
            (347, 8),
        )
        for key, mask in modifiers:
            with self.subTest(key=key):
                with Shitty(columns=8, rows=3, save_lines=8) as terminal:
                    terminal.write(
                        b"one\r\ntwo\r\nthree\r\nfour\x1b[>8u"
                    )
                    terminal.page_up()

                    terminal.frontend_key_event(key, 1, modifiers=mask)

                    self.assertEqual(terminal.snapshot().view_offset, 1)
                    packet = terminal.read_input()
                    self.assertTrue(packet.startswith(b"\x1b["))
                    self.assertTrue(packet.endswith(b"u"))

        with Shitty(columns=8, rows=3, save_lines=8) as terminal:
            terminal.write(b"one\r\ntwo\r\nthree\r\nfour\x1b[>8u")
            terminal.page_up()

            terminal.frontend_key_event(257, 1)

            self.assertEqual(terminal.snapshot().view_offset, 0)
            self.assertEqual(terminal.read_input(), b"\x1b[13u")


if __name__ == "__main__":
    unittest.main()
