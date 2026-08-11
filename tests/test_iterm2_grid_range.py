# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

"""Public adaptations of the first five iTerm2 grid-range cases."""

import unittest

from harness import Shitty


PORTED_CASES = (
    "test_emptyOuter_returnsEmpty",
    "test_invalidOuter_returnsEmpty",
    "test_outerWithNoExclusions_returnsOuter",
    "test_emptyExclusionInList_isIgnored",
    "test_exclusionInMiddle_splitsInTwo",
)


def osc133(action, options=b""):
    suffix = b";" + options if options else b""
    return b"\x1b]133;" + action + suffix + b"\x1b\\"


def current_line_action(terminal, column, row):
    """Exercise Shitty's nearest public selection action.

    Shitty has no select-current-command action yet.  Cycling the existing
    action reaches whole-line selection and deliberately exposes that gap:
    prompt and right-prompt cells are still returned with the input.
    """
    terminal.select_start(column, row)
    terminal.select_extend(column, row, cycle=True)
    terminal.select_extend(column, row, cycle=True)
    return terminal.select_finish()


class ITerm2GridRangeTest(unittest.TestCase):
    def test_upstream_inventory_has_first_five_distinct_cases(self):
        self.assertEqual(len(PORTED_CASES), 5)
        self.assertEqual(len(set(PORTED_CASES)), 5)

    @unittest.expectedFailure
    def test_empty_current_command_range_selects_nothing(self):
        with Shitty(columns=20, rows=1, save_lines=0) as terminal:
            terminal.write(osc133(b"P", b"k=i") + b"$ " + osc133(b"B"))
            self.assertEqual(current_line_action(terminal, 0, 0), b"")

    @unittest.expectedFailure
    def test_missing_current_command_range_selects_nothing(self):
        with Shitty(columns=24, rows=1, save_lines=0) as terminal:
            terminal.write(b"not a semantic command")
            self.assertEqual(current_line_action(terminal, 4, 0), b"")

    @unittest.expectedFailure
    def test_current_command_without_exclusions_selects_the_whole_outer_range(self):
        with Shitty(columns=24, rows=2, save_lines=0) as terminal:
            terminal.write(
                osc133(b"P", b"k=i")
                + b"$ "
                + osc133(b"B")
                + b"echo one\r\n"
                + osc133(b"P", b"k=s")
                + b"> "
                + osc133(b"B")
                + b"echo two"
            )
            self.assertEqual(
                current_line_action(terminal, 4, 0),
                b"echo one\necho two",
            )

    @unittest.expectedFailure
    def test_empty_right_prompt_exclusion_does_not_remove_input(self):
        with Shitty(columns=20, rows=1, save_lines=0) as terminal:
            terminal.write(
                osc133(b"P", b"k=i")
                + b"$ "
                + osc133(b"B")
                + b"left"
                + osc133(b"P", b"k=r")
                + osc133(b"B")
                + b"right"
            )
            self.assertEqual(current_line_action(terminal, 4, 0), b"leftright")

    @unittest.expectedFailure
    def test_middle_right_prompt_exclusion_splits_and_rejoins_same_row_input(self):
        with Shitty(columns=20, rows=1, save_lines=0) as terminal:
            terminal.write(
                osc133(b"P", b"k=i")
                + b"$ "
                + osc133(b"B")
                + b"left"
                + osc133(b"P", b"k=r")
                + b"RP"
                + osc133(b"B")
                + b"right"
            )
            self.assertEqual(current_line_action(terminal, 4, 0), b"leftright")


if __name__ == "__main__":
    unittest.main()
