# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

"""Public streaming adaptations of the first 13 iTerm2 DCS parser cases."""

import unittest

from harness import Shitty


PORTED_CASES = (
    "testDCS",
    "testDCSControl",
    "testDCSBackspace",
    "testDCSIntermediate",
    "testDCSMultipleIntermediates",
    "testDCSIntermediateIgnore",
    "testDCSIntermediateIgnoreIgnore",
    "testDCSIntermediateIgnoreMany",
    "testDCSIntermediateIgnoreIgnoreEsc",
    "testDCSIntermediateIgnoreIgnoreST",
    "testDCSIntermediateIgnoreIgnoreEscAsciiST",
    "testDCSIntermediatePassthrough",
    "testDCSIntermediatePassthroughEsc",
)

TEXT_X = [("text", b"X")]


def assert_pending_then_recovers(testcase, prefix, suffix, expected=TEXT_X):
    with Shitty(columns=8, rows=3, save_lines=0) as terminal:
        terminal.parser_trace_on()
        terminal.write(prefix)
        testcase.assertEqual(terminal.parser_trace(), [])
        terminal.write(suffix)
        testcase.assertEqual(terminal.parser_trace(), expected)


class ITerm2DCSParserTest(unittest.TestCase):
    def test_upstream_inventory_has_first_13_distinct_cases(self):
        self.assertEqual(len(PORTED_CASES), 13)
        self.assertEqual(len(set(PORTED_CASES)), 13)

    def test_dcs_introducer_remains_pending_until_terminated(self):
        assert_pending_then_recovers(self, b"\x1bP", b"\x1b\\X")

    def test_dcs_entry_ignores_embedded_line_feed(self):
        assert_pending_then_recovers(self, b"\x1bP\n", b"\x1b\\X")

    def test_dcs_entry_ignores_delete(self):
        assert_pending_then_recovers(self, b"\x1bP\x7f", b"\x1b\\X")

    def test_one_dcs_intermediate_remains_pending(self):
        assert_pending_then_recovers(self, b"\x1bP ", b"\x1b\\X")

    def test_multiple_dcs_intermediates_remain_pending(self):
        assert_pending_then_recovers(self, b"\x1bP !", b"\x1b\\X")

    def test_parameter_after_intermediate_enters_ignore(self):
        assert_pending_then_recovers(self, b"\x1bP 0", b"\x1b\\X")

    def test_more_parameters_remain_ignored(self):
        assert_pending_then_recovers(self, b"\x1bP 01", b"\x1b\\X")

    def test_controls_and_printable_bytes_remain_ignored(self):
        assert_pending_then_recovers(
            self,
            b"\x1bP 0\n\x19\x1c0",
            b"\x1b\\X",
        )

    def test_escape_at_end_of_ignored_dcs_remains_pending(self):
        assert_pending_then_recovers(self, b"\x1bP 01\x1b", b"\\X")

    def test_st_terminates_ignored_dcs_without_dispatch(self):
        assert_pending_then_recovers(self, b"\x1bP 01\x1b\\", b"X")

    @unittest.expectedFailure
    def test_non_st_escape_inside_ignored_dcs_stays_in_the_string(self):
        # This is iTerm2/Kitty behavior.  Six other implementations and the
        # DEC parser model abort the DCS and process ESC a as a new escape.
        with Shitty(columns=8, rows=3, save_lines=0) as terminal:
            terminal.parser_trace_on()
            terminal.write(b"\x1bP 01\x1babc\x1b\\")
            self.assertEqual(terminal.parser_trace(), [])
            terminal.write(b"X")
            self.assertEqual(terminal.parser_trace(), TEXT_X)

    def test_intermediate_final_enters_dcs_passthrough(self):
        assert_pending_then_recovers(
            self,
            b"\x1bP x",
            b"\x1b\\X",
            [("dcs", b" x"), ("text", b"X")],
        )

    def test_escape_at_end_of_dcs_passthrough_remains_pending(self):
        assert_pending_then_recovers(
            self,
            b"\x1bP x\x1b",
            b"\\X",
            [("dcs", b" x"), ("text", b"X")],
        )


if __name__ == "__main__":
    unittest.main()
