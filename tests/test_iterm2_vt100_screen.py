# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

"""Public adaptations of the first two iTerm2 VT100Screen cases."""

import unittest

from harness import Shitty


PORTED_CASES = (
    "testResizeNotes",
    "testSwitchingScreenBuffersRefreshesChangedKeyReportingFlags",
)


class ITerm2VT100ScreenTest(unittest.TestCase):
    def test_upstream_inventory_has_first_two_distinct_cases(self):
        self.assertEqual(len(PORTED_CASES), 2)
        self.assertEqual(len(set(PORTED_CASES)), 2)

    def test_primary_semantic_range_survives_alt_screen_resize_reflow(self):
        with Shitty(columns=5, rows=4, save_lines=10) as terminal:
            terminal.write(
                b"abcde"
                b"\x1b]133;P\x07fgh\x1b]133;D\x07"
                b"\r\nijkl\r\n"
            )
            before = terminal.model_snapshot()
            self.assertEqual(before.lines, ["abcde", "fgh  ", "ijkl ", "     "])
            self.assertEqual(
                [before.cell(column, 1).semantic for column in range(5)],
                [1, 1, 1, 0, 0],
            )

            terminal.write(b"\x1b[?1049h")
            terminal.resize(4, 4)
            terminal.write(b"\x1b[?1049l")

            after = terminal.model_snapshot()
            self.assertEqual(after.lines, ["abcd", "efgh", "ijkl", "    "])
            self.assertEqual(
                [after.cell(column, 1).semantic for column in range(4)],
                [0, 1, 1, 1],
            )

    def test_screen_switch_restores_each_kitty_keyboard_stack(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[>1u\x1b[?1049h\x1b[>2u")

            terminal.write(b"\x1b[?1049l")
            self.assertEqual(terminal.state()[3], 1)
            terminal.write(b"\x1b[?u")
            self.assertEqual(terminal.read_input(), b"\x1b[?1u")

            terminal.write(b"\x1b[?1049h")
            self.assertEqual(terminal.state()[3], 2)
            terminal.write(b"\x1b[?u")
            self.assertEqual(terminal.read_input(), b"\x1b[?2u")


if __name__ == "__main__":
    unittest.main()
