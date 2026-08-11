# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

"""Public adaptations of the first two iTerm2 VT100Screen cases."""

import unittest

from harness import Shitty


PORTED_CASES = (
    "testResizeNotes",
    "testSwitchingScreenBuffersRefreshesChangedKeyReportingFlags",
    "testSwitchingScreenBuffersDoesNotRefreshUnchangedKeyReportingFlags",
    "testScreenCharArrayForLineContentIdentityMatchesSeparateCalls",
    "testResizeNoteInPrimaryWhileInAltAndSomeHistory",
    "testResizeNoteInPrimaryWhileInAltAndPushingSomePrimaryIncludingWholeNoteIntoHistory",
    "testResizeNoteInPrimaryWhileInAltAndPushingSomePrimaryIncludingPartOfNoteIntoHistory",
    "testResizeNoteInAlternateThatGetsTruncatedByShrinkage",
    "testResizeWithNoteFirstLine",
    "testResizeWithNoteFirstLinePlusFirstCharacterOfSecondLine",
    "testResizeWithNoteFirstTwoCharactersOfSecondLine",
    "testResizeWithNoteSecondLine",
    "testResizeWithNoteLastFourCharactersOfSecondLine",
    "testResizeWithNoteSecondCharacterOfSecondLineToSecondCharacterOfThirdLine",
    "testResizeWithNoteSecondAndThirdLines",
    "testResizeWithNoteSecondThroughFourthLines",
    "testResizeWithNoteSecondThroughFifthLines",
    "testResizeWithNoteSecondCharacterOfSecondLineThroughFirstCharacterOfFifthLine",
    "testResizeWithNoteThirdLineThroughFifthLine",
    "testResizeWithNoteThirdLineThroughMiddleOfFifthLine",
    "testResizeWithNoteFifthLine",
    "testResizeWithNoteAllLines",
)


class ITerm2VT100ScreenTest(unittest.TestCase):
    def test_upstream_inventory_has_first_22_distinct_cases(self):
        self.assertEqual(len(PORTED_CASES), 22)
        self.assertEqual(len(set(PORTED_CASES)), 22)

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

    def test_screen_switch_with_equal_flags_keeps_the_public_report(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[?1049h\x1b[?1049l")
            self.assertEqual(terminal.state()[3], 0)

            terminal.write(b"\x1b[=1;1u")
            self.assertEqual(terminal.state()[3], 1)
            terminal.write(b"\x1b[?1049h\x1b[?1049l\x1b[?u")
            self.assertEqual(terminal.state()[3], 1)
            self.assertEqual(terminal.read_input(), b"\x1b[?1u")

    def test_repeated_line_reads_match_across_the_history_boundary(self):
        with Shitty(columns=5, rows=4, save_lines=10) as terminal:
            terminal.write(b"abcdefgh\r\nijkl\r\nhello world")
            first = terminal.model_snapshot()
            second = terminal.model_snapshot()
            self.assertEqual(first.lines, second.lines)
            self.assertEqual(
                [first.cell(column, row) for row in range(4) for column in range(5)],
                [second.cell(column, row) for row in range(4) for column in range(5)],
            )

            terminal.wheel_up(20)
            history_first = terminal.model_snapshot()
            history_second = terminal.model_snapshot()
            self.assertEqual(history_first.lines, ["abcde", "fgh  ", "ijkl ", "hello"])
            self.assertEqual(history_first.lines, history_second.lines)
            self.assertEqual(
                [history_first.cell(column, row) for row in range(4) for column in range(5)],
                [history_second.cell(column, row) for row in range(4) for column in range(5)],
            )

    @staticmethod
    def _visible_semantic_chars(terminal):
        snapshot = terminal.model_snapshot()
        return "".join(
            snapshot.cell(column, row).char
            for row in range(snapshot.rows)
            for column in range(snapshot.columns)
            if snapshot.cell(column, row).semantic
        )

    def _assert_history_semantic_resize(self, start, end, width, visible_after):
        payload = b"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
        marked = payload[start:end].decode("ascii")
        with Shitty(columns=5, rows=4, save_lines=20) as terminal:
            terminal.write(
                payload[:start]
                + b"\x1b]133;P\x07"
                + payload[start:end]
                + b"\x1b]133;D\x07"
                + payload[end:]
            )
            self.assertEqual(self._visible_semantic_chars(terminal), marked)

            terminal.write(b"\x1b[?1049h")
            terminal.resize(width, 4)
            terminal.write(b"\x1b[?1049l")
            self.assertEqual(self._visible_semantic_chars(terminal), visible_after)

            seen = set(self._visible_semantic_chars(terminal))
            for _ in range(terminal.scrollback_state()[0]):
                terminal.wheel_up(1)
                seen.update(self._visible_semantic_chars(terminal))
            self.assertEqual(seen, set(marked))

    def test_primary_semantic_range_survives_inactive_resize_with_history(self):
        self._assert_history_semantic_resize(24, 29, 4, "YZ012")

    def test_primary_semantic_range_can_move_wholly_into_history(self):
        self._assert_history_semantic_resize(20, 24, 3, "")

    def test_primary_semantic_range_can_split_across_history_and_screen(self):
        self._assert_history_semantic_resize(22, 27, 3, "YZ0")

    def test_alternate_semantic_range_is_clipped_by_nonreflowing_shrink(self):
        with Shitty(columns=5, rows=4, save_lines=0) as terminal:
            terminal.write(
                b"\x1b[?1049hABCDE"
                b"\x1b]133;P\x07FGHIJKLMNOPQRST\x1b]133;D\x07"
            )
            terminal.resize(3, 4)
            self.assertEqual(terminal.model_snapshot().lines, ["ABC", "FGH", "KLM", "PQR"])
            self.assertEqual(self._visible_semantic_chars(terminal), "FGHKLMPQR")

    def _assert_semantic_range_reflow_round_trip(self, start, end):
        payload = b"ABCDEFGHIJKLMNOPQRSTUVWXY"
        marked = payload[start:end].decode("ascii")
        with Shitty(columns=5, rows=10, save_lines=10) as terminal:
            terminal.write(payload)
            row, column = divmod(start, 5)
            terminal.write(
                f"\x1b[{row + 1};{column + 1}H".encode()
                + b"\x1b]133;P\x07"
                + payload[start:end]
                + b"\x1b]133;D\x07"
            )
            self.assertEqual(self._visible_semantic_chars(terminal), marked)

            terminal.write(b"\x1b[?1049h")
            terminal.resize(3, 10)
            terminal.resize(5, 10)
            terminal.write(b"\x1b[?1049l")
            self.assertEqual(terminal.model_snapshot().lines[:5], [
                "ABCDE",
                "FGHIJ",
                "KLMNO",
                "PQRST",
                "UVWXY",
            ])
            self.assertEqual(self._visible_semantic_chars(terminal), marked)

    def test_semantic_round_trip_first_line(self):
        self._assert_semantic_range_reflow_round_trip(0, 5)

    def test_semantic_round_trip_first_line_and_second_line_prefix(self):
        self._assert_semantic_range_reflow_round_trip(0, 7)

    def test_semantic_round_trip_second_line_prefix(self):
        self._assert_semantic_range_reflow_round_trip(5, 8)

    def test_semantic_round_trip_second_line(self):
        self._assert_semantic_range_reflow_round_trip(5, 10)

    def test_semantic_round_trip_second_line_suffix(self):
        self._assert_semantic_range_reflow_round_trip(7, 10)

    def test_semantic_round_trip_crosses_second_and_third_lines(self):
        self._assert_semantic_range_reflow_round_trip(7, 12)

    def test_semantic_round_trip_second_and_third_lines(self):
        self._assert_semantic_range_reflow_round_trip(5, 15)

    def test_semantic_round_trip_second_through_fourth_lines(self):
        self._assert_semantic_range_reflow_round_trip(5, 20)

    def test_semantic_round_trip_second_through_fifth_lines(self):
        self._assert_semantic_range_reflow_round_trip(5, 25)

    def test_semantic_round_trip_second_line_suffix_through_fifth_prefix(self):
        self._assert_semantic_range_reflow_round_trip(7, 22)

    def test_semantic_round_trip_fourth_and_fifth_lines(self):
        self._assert_semantic_range_reflow_round_trip(15, 25)

    def test_semantic_round_trip_fourth_line_through_fifth_prefix(self):
        self._assert_semantic_range_reflow_round_trip(15, 23)

    def test_semantic_round_trip_fifth_line(self):
        self._assert_semantic_range_reflow_round_trip(20, 25)

    def test_semantic_round_trip_all_lines(self):
        self._assert_semantic_range_reflow_round_trip(0, 25)


if __name__ == "__main__":
    unittest.main()
