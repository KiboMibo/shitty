# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

"""Public adaptations of the first three iTerm2 Xterm parser cases."""

import unittest

from harness import Shitty


PORTED_CASES = (
    "testNoModeYet",
    "testWellFormedSetWindowTitleTerminatedByBell",
    "testWellFormedSetWindowTitleTerminatedByST",
)


class ITerm2XtermParserTest(unittest.TestCase):
    def test_upstream_inventory_has_first_three_distinct_cases(self):
        self.assertEqual(len(PORTED_CASES), 3)
        self.assertEqual(len(set(PORTED_CASES)), 3)

    def test_osc_introducer_waits_and_a_new_complete_osc_recovers(self):
        with Shitty(columns=8, rows=2, save_lines=0) as terminal:
            terminal.parser_trace_on()
            terminal.write(b"\x1b]")
            self.assertEqual(terminal.parser_trace(), [])
            terminal.write(b"\x1b]0;title\x07")
            self.assertEqual(terminal.window_title(), "title")
            self.assertEqual(terminal.parser_trace(), [("osc", b"0;title")])

    def test_window_title_terminated_by_bell(self):
        with Shitty(columns=8, rows=2, save_lines=0) as terminal:
            terminal.parser_trace_on()
            terminal.write(b"\x1b]0;title\x07")
            self.assertEqual(terminal.window_title(), "title")
            self.assertEqual(terminal.parser_trace(), [("osc", b"0;title")])

    def test_window_title_terminated_by_st(self):
        with Shitty(columns=8, rows=2, save_lines=0) as terminal:
            terminal.parser_trace_on()
            terminal.write(b"\x1b]0;title\x1b\\")
            self.assertEqual(terminal.window_title(), "title")
            self.assertEqual(terminal.parser_trace(), [("osc", b"0;title")])


if __name__ == "__main__":
    unittest.main()
