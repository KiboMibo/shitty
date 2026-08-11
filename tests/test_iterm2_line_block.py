# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

"""Public adaptations of the first 17 iTerm2 LineBlock cases."""

import unittest

from harness import Shitty


PORTED_CASES = (
    "testInitWithRawBufferSizeCreatesEmptyBlock",
    "testAppendLineStringSucceedsWithinCapacityHardEOL",
    "testAppendLineStringSucceedsWithinCapacitySoftEOL",
    "testAppendLineStringFailsWhenExceededCapacity",
    "testAppendToExistingPartialLinePathUpdatesCachedNumLines",
    "testWrappedLineStringWithoutDWCProducesCorrectSegments",
    "testWrappedLineStringWithDWCSoftAndHardEOL",
    "testGetNumLinesWithWrapWidthCachesResult",
    "testTotallyUncachedNumLinesBypassesCache",
    "testHasCachedNumLinesForWidth",
    "testPopLastLineUpToWidthSplitsLongRawLine",
    "testPopLastLineUpToWidthReturnsWholeLineWhenShort",
    "testRemoveLastWrappedLinesWithinSingleBlock",
    "testRemoveLastWrappedLinesRemovesEntireBlockWhenExactCount",
    "testRemoveLastRawLineWithMultipleEntries",
    "testRemoveLastRawLineOnSingleEntryResetsBlock",
    "testLengthOfLastLine",
)


class ITerm2LineBlockTest(unittest.TestCase):
    def test_upstream_inventory_has_first_17_distinct_cases(self):
        self.assertEqual(len(PORTED_CASES), 17)
        self.assertEqual(len(set(PORTED_CASES)), 17)

    def test_fresh_terminal_has_empty_history_and_blank_storage(self):
        with Shitty(columns=5, rows=3, save_lines=4) as terminal:
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.lines, ["     "] * 3)
            self.assertEqual(terminal.scrollback_state()[0], 0)
            self.assertEqual(terminal.all_text(), ("", "", ""))
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 0))

    def test_hard_eol_commits_one_independent_logical_line(self):
        with Shitty(columns=5, rows=2, save_lines=4) as terminal:
            terminal.write(b"abc\r\n")
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.lines, ["abc  ", "     "])
            self.assertFalse(snapshot.cell(4, 0).wrapped)
            self.assertEqual(terminal.all_text(), ("abc", ""))

    def test_soft_eol_keeps_wrapped_rows_in_one_logical_line(self):
        with Shitty(columns=5, rows=2, save_lines=4) as terminal:
            terminal.write(b"abcdef")
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.lines, ["abcde", "f    "])
            self.assertTrue(snapshot.cell(4, 0).wrapped)
            self.assertFalse(snapshot.cell(4, 1).wrapped)
            terminal.select_start(0, 0)
            terminal.select_update(1, 1)
            self.assertEqual(terminal.select_finish(), b"abcdef")

    def test_line_larger_than_one_storage_chunk_is_not_lost(self):
        payload = ("0123456789" * 901) + "xyz"
        with Shitty(columns=80, rows=2, save_lines=200) as terminal:
            terminal.write(payload.encode())
            self.assertGreater(terminal.scrollback_state()[0], 100)
            self.assertEqual("".join(terminal.all_text()).rstrip(), payload)

    def test_appending_to_partial_line_invalidates_prior_reflow_result(self):
        with Shitty(columns=6, rows=4, save_lines=8) as terminal:
            terminal.write(b"Hello")
            terminal.resize(3, 4)
            self.assertEqual(
                terminal.model_snapshot().lines[:2],
                ["Hel", "lo "],
            )

            terminal.write(b" World")
            terminal.resize(12, 4)
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.lines[0], "Hello World ")
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (11, 0))
            self.assertFalse(snapshot.cell(11, 0).wrapped)

    def test_ascii_wrapping_produces_soft_then_hard_segments(self):
        with Shitty(columns=3, rows=3, save_lines=4) as terminal:
            terminal.write(b"ABCDE\r\n")
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.lines, ["ABC", "DE ", "   "])
            self.assertTrue(snapshot.cell(2, 0).wrapped)
            self.assertFalse(snapshot.cell(2, 1).wrapped)
            self.assertEqual(terminal.all_text(), ("ABC", "DE", ""))

    def test_wide_glyph_wraps_as_one_cell_pair_with_hard_final_eol(self):
        with Shitty(columns=3, rows=3, save_lines=4) as terminal:
            terminal.write("A界BC\r\n".encode())
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.lines, ["A界 ", "BC ", "   "])
            self.assertTrue(snapshot.cell(1, 0).double_width)
            self.assertTrue(snapshot.cell(2, 0).double_width_continuation)
            self.assertTrue(snapshot.cell(2, 0).wrapped)
            self.assertFalse(snapshot.cell(2, 1).wrapped)

    def test_repeated_line_count_observation_at_one_width_is_stable(self):
        with Shitty(columns=7, rows=4, save_lines=8) as terminal:
            terminal.write(b"ABCDEFG")
            terminal.resize(3, 4)
            digest = terminal.model_digest()
            first = terminal.model_snapshot()
            second = terminal.model_snapshot()
            self.assertEqual(first.lines, ["ABC", "DEF", "G  ", "   "])
            self.assertEqual(second.lines, first.lines)
            self.assertEqual(terminal.model_digest(), digest)

    def test_recomputed_line_count_matches_after_width_round_trip(self):
        with Shitty(columns=7, rows=4, save_lines=8) as terminal:
            terminal.write(b"ABCDEFG")
            terminal.resize(3, 4)
            first = terminal.model_snapshot()
            terminal.resize(7, 4)
            self.assertEqual(terminal.model_snapshot().lines[0], "ABCDEFG")
            terminal.resize(3, 4)
            second = terminal.model_snapshot()
            self.assertEqual(second.lines, first.lines)
            self.assertEqual(
                [second.cell(2, row).wrapped for row in range(4)],
                [True, True, False, False],
            )

    def test_reflow_results_are_keyed_by_the_active_width(self):
        with Shitty(columns=7, rows=4, save_lines=8) as terminal:
            terminal.write(b"ABCDEFG")
            terminal.resize(3, 4)
            self.assertEqual(
                terminal.model_snapshot().lines[:3],
                ["ABC", "DEF", "G  "],
            )
            terminal.resize(4, 4)
            self.assertEqual(
                terminal.model_snapshot().lines[:2],
                ["ABCD", "EFG "],
            )
            terminal.resize(3, 4)
            self.assertEqual(
                terminal.model_snapshot().lines[:3],
                ["ABC", "DEF", "G  "],
            )

    def test_height_growth_pops_only_last_segment_of_long_history_line(self):
        with Shitty(columns=4, rows=1, save_lines=8) as terminal:
            terminal.write(b"ABCDEFGHIJ\r\n")
            self.assertEqual(terminal.scrollback_state()[0], 3)
            terminal.resize(4, 2)
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.lines, ["IJ  ", "    "])
            self.assertEqual(terminal.scrollback_state()[0], 2)
            self.assertEqual(terminal.all_text(), ("ABCD", "EFGH", "IJ", ""))

    def test_height_growth_pops_whole_short_history_line(self):
        with Shitty(columns=10, rows=1, save_lines=4) as terminal:
            terminal.write(b"Hello\r\n")
            self.assertEqual(terminal.scrollback_state()[0], 1)
            terminal.resize(10, 2)
            self.assertEqual(
                terminal.model_snapshot().lines,
                ["Hello     ", "          "],
            )
            self.assertEqual(terminal.scrollback_state()[0], 0)

    def test_height_growth_removes_two_newest_wrapped_segments(self):
        with Shitty(columns=3, rows=1, save_lines=8) as terminal:
            terminal.write(b"ABCDEFGHIJ\r\n")
            terminal.resize(3, 3)
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.lines, ["GHI", "J  ", "   "])
            self.assertEqual(terminal.scrollback_state()[0], 2)
            self.assertTrue(snapshot.cell(2, 0).wrapped)
            self.assertFalse(snapshot.cell(2, 1).wrapped)

    def test_height_growth_can_consume_all_wrapped_history_segments(self):
        with Shitty(columns=3, rows=1, save_lines=8) as terminal:
            terminal.write(b"ABCDEFGHIJ\r\n")
            terminal.resize(3, 5)
            snapshot = terminal.model_snapshot()
            self.assertEqual(
                snapshot.lines,
                ["ABC", "DEF", "GHI", "J  ", "   "],
            )
            self.assertEqual(terminal.scrollback_state()[0], 0)
            self.assertEqual(
                [snapshot.cell(2, row).wrapped for row in range(5)],
                [True, True, True, False, False],
            )

    def test_height_growth_removes_only_newest_of_multiple_raw_lines(self):
        with Shitty(columns=10, rows=1, save_lines=4) as terminal:
            terminal.write(b"FirstLine\r\nSecondLine\r\n")
            terminal.resize(10, 2)
            self.assertEqual(
                terminal.model_snapshot().lines,
                ["SecondLine", "          "],
            )
            self.assertEqual(terminal.scrollback_state()[0], 1)
            terminal.wheel_up()
            self.assertEqual(
                terminal.model_snapshot().lines,
                ["FirstLine ", "SecondLine"],
            )

    def test_consuming_only_raw_history_line_resets_history_state(self):
        with Shitty(columns=10, rows=1, save_lines=4) as terminal:
            terminal.write(b"Test\r\n")
            terminal.resize(10, 2)
            self.assertEqual(terminal.scrollback_state()[0], 0)
            self.assertEqual(terminal.all_text(), ("Test", ""))

            terminal.write(b"Next\r\n")
            self.assertEqual(terminal.scrollback_state()[0], 1)
            self.assertEqual(terminal.all_text(), ("Test", "Next", ""))

    def test_last_raw_line_length_tracks_the_newest_hard_line(self):
        with Shitty(columns=15, rows=3, save_lines=4) as terminal:
            terminal.write(b"Hello\r\nWorldWide")
            snapshot = terminal.model_snapshot()
            self.assertEqual(terminal.all_text(), ("Hello", "WorldWide", ""))
            self.assertEqual(
                [
                    sum(snapshot.cell(column, row).drawn for column in range(15))
                    for row in range(3)
                ],
                [5, 9, 0],
            )


if __name__ == "__main__":
    unittest.main()
