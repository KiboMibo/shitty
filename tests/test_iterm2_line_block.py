# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

"""Public adaptations of the first 37 iTerm2 LineBlock cases."""

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
    "testLengthOfLastLineWrappedToWidthCalculatesCorrectSegments",
    "testDropLinesLessThanSpansAdjustsFirstEntryAndBufferStartOffset",
    "testDropLinesEqualToSpansEmptiesBlockEntry",
    "testDropLinesMoreThanAvailableDropsEntireBuffer",
    "testIsEmptyAndAllLinesAreEmptyWhenNoEntries",
    "testAllLinesAreEmptyWhenEntriesZeroLength",
    "testNumRawLinesStartOffsetAndOffsets",
    "testRawLineAndOffsetAfterPartialDrop",
    "testDictionaryRoundTripPreservesContents",
    "testNumberOfLeadingEmptyLines",
    "testNumberOfTrailingEmptyLines",
    "testContainsAnyNonEmptyLine",
    "testCopyDeepProducesIndependentBlock",
    "testCowCopySetsHasBeenCopiedAndProgenitor",
    "testOffsetOfWrappedLineSimpleMultiplicationPath",
    "testOffsetOfWrappedLineWithDWCPath",
    "testNumberOfFullLinesFromOffsetSimpleDivision",
    "testNumberOfFullLinesFromOffsetDWCImpl",
    "testLocationOfRawLineForWidthConsumesWrappedLines",
    "testLocationOfRawLineForWidthHandlesEmptyLinesAndNumEmptyLines",
)


class ITerm2LineBlockTest(unittest.TestCase):
    def test_upstream_inventory_has_first_37_distinct_cases(self):
        self.assertEqual(len(PORTED_CASES), 37)
        self.assertEqual(len(set(PORTED_CASES)), 37)

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

    def test_last_line_wrap_count_tracks_width_specific_segments(self):
        with Shitty(columns=10, rows=4, save_lines=8) as terminal:
            terminal.write(b"ABCDEFGHIJ\r\n")

            terminal.resize(4, 4)
            self.assertEqual(
                terminal.model_snapshot().lines,
                ["ABCD", "EFGH", "IJ  ", "    "],
            )
            terminal.resize(5, 4)
            self.assertEqual(
                terminal.model_snapshot().lines,
                ["ABCDE", "FGHIJ", "     ", "     "],
            )
            terminal.resize(20, 4)
            self.assertEqual(
                terminal.model_snapshot().lines[0],
                "ABCDEFGHIJ          ",
            )

    def test_bounded_history_drops_only_oldest_wrapped_prefix(self):
        with Shitty(columns=3, rows=1, save_lines=2) as terminal:
            terminal.write(b"ABCDEFGHIJ\r\n")
            self.assertEqual(terminal.scrollback_state()[0], 2)
            self.assertEqual(terminal.all_text(), ("GHI", "J", ""))

            terminal.wheel_up(10)
            self.assertEqual(terminal.model_snapshot().lines, ["GHI"])

    def test_zero_history_limit_drops_exactly_the_available_wrapped_line(self):
        with Shitty(columns=3, rows=1, save_lines=0) as terminal:
            terminal.write(b"ABCDEFGHIJ\r\n")
            self.assertEqual(terminal.scrollback_state()[0], 0)
            self.assertEqual(terminal.all_text(), ("",))
            self.assertEqual(terminal.model_snapshot().lines, ["   "])

    def test_clearing_more_history_than_exists_is_idempotent(self):
        with Shitty(columns=6, rows=2, save_lines=8) as terminal:
            terminal.write(b"Only\r\nTail\r\n")
            self.assertEqual(terminal.scrollback_state()[0], 1)

            terminal.write(b"\x1b[3J")
            self.assertEqual(terminal.scrollback_state()[0], 0)
            first = terminal.model_digest()
            terminal.write(b"\x1b[3J")
            self.assertEqual(terminal.scrollback_state()[0], 0)
            self.assertEqual(terminal.model_digest(), first)

    def test_fresh_storage_has_no_content_and_no_history(self):
        with Shitty(columns=5, rows=3, save_lines=4) as terminal:
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.lines, ["     "] * 3)
            self.assertEqual(terminal.all_text(), ("", "", ""))
            self.assertEqual(terminal.scrollback_state()[0], 0)

    def test_zero_length_hard_lines_remain_distinct_empty_entries(self):
        with Shitty(columns=5, rows=2, save_lines=4) as terminal:
            terminal.write(b"\r\n\r\n\r\n")
            self.assertEqual(terminal.all_text(), ("", "", "", ""))
            self.assertEqual(terminal.scrollback_state()[0], 2)
            self.assertEqual(terminal.model_snapshot().lines, ["     ", "     "])

    def test_hard_lines_keep_public_order_and_cumulative_boundaries(self):
        with Shitty(columns=10, rows=7, save_lines=4) as terminal:
            terminal.write(b"Apple\r\nBanana\r\nCherry")
            self.assertEqual(
                terminal.all_text()[:3],
                ("Apple", "Banana", "Cherry"),
            )
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.lines[:3], [
                "Apple     ",
                "Banana    ",
                "Cherry    ",
            ])
            self.assertFalse(snapshot.cell(9, 0).wrapped)
            self.assertFalse(snapshot.cell(9, 1).wrapped)

    def test_partial_front_drop_exposes_the_exact_surviving_raw_suffix(self):
        with Shitty(columns=3, rows=1, save_lines=2) as terminal:
            terminal.write(b"AB\r\nCDEF\r\nGHIJKLMN\r\n")
            self.assertEqual(terminal.scrollback_state()[0], 2)
            self.assertEqual(terminal.all_text(), ("JKL", "MN", ""))

            terminal.resize(3, 2)
            terminal.wheel_up(10)
            terminal.select_start(0, 0)
            terminal.select_update(3, 1)
            self.assertEqual(terminal.select_finish(), b"JKLMN")

    def test_primary_round_trip_preserves_text_boundaries_and_hyperlink(self):
        with Shitty(columns=10, rows=4, save_lines=8) as terminal:
            terminal.write(
                b"\x1b]8;;https://example.test\x1b\\One"
                b"\x1b]8;;\x1b\\\r\nTwoTwo\r\nThree"
            )
            expected = terminal.model_digest()
            self.assertEqual(terminal.hyperlink(0, 0), "https://example.test")

            terminal.write(b"\x1b[?1049hAlternate\x1b[?1049l")
            self.assertEqual(terminal.model_digest(), expected)
            self.assertEqual(terminal.all_text(), ("One", "TwoTwo", "Three", ""))
            self.assertEqual(terminal.hyperlink(0, 0), "https://example.test")

    def test_leading_empty_hard_lines_survive_reflow_before_content(self):
        with Shitty(columns=5, rows=6, save_lines=8) as terminal:
            terminal.write(b"\r\n\r\nabc\r\n\r\n")
            self.assertEqual(terminal.all_text(), ("", "", "abc", "", "", ""))

            terminal.resize(7, 6)
            self.assertEqual(terminal.all_text(), ("", "", "abc", "", "", ""))
            self.assertEqual(terminal.model_snapshot().lines[2], "abc    ")

    def test_trailing_empty_run_resets_when_new_content_arrives(self):
        with Shitty(columns=5, rows=6, save_lines=8) as terminal:
            terminal.write(b"abc\r\n\r\n\r\n")
            self.assertEqual(terminal.all_text()[:4], ("abc", "", "", ""))

            terminal.write(b"xyz")
            self.assertEqual(terminal.all_text()[:4], ("abc", "", "", "xyz"))
            self.assertEqual(terminal.model_snapshot().lines[3], "xyz  ")

    def test_nonempty_content_presence_toggles_through_public_erase(self):
        with Shitty(columns=6, rows=4, save_lines=0) as terminal:
            terminal.write(b"\r\n\r\n")
            self.assertEqual(tuple(filter(None, terminal.all_text())), ())

            terminal.write(b"hello")
            self.assertEqual(tuple(filter(None, terminal.all_text())), ("hello",))
            terminal.write(b"\x1b[2J\x1b[H")
            self.assertEqual(tuple(filter(None, terminal.all_text())), ())

    def test_two_sessions_mutate_their_screen_storage_independently(self):
        with Shitty(columns=8, rows=3, save_lines=4) as terminal:
            terminal.write(b"First\r\nPartial")
            terminal.new_session()
            terminal.write(b"Other")
            self.assertEqual(terminal.model_snapshot().lines[0], "Other   ")

            terminal.write_to(0, b"Extra")
            self.assertEqual(terminal.model_snapshot().lines[0], "Other   ")
            terminal.chord_prev_tab()
            self.assertEqual(
                terminal.model_snapshot().lines,
                ["First   ", "PartialE", "xtra    "],
            )

    def test_published_snapshot_is_independent_of_later_mutation(self):
        with Shitty(columns=6, rows=3, save_lines=4) as terminal:
            terminal.write(b"First")
            before = terminal.model_snapshot()
            before_lines = list(before.lines)

            terminal.write(b"Second")
            after = terminal.model_snapshot()
            self.assertEqual(before.lines, before_lines)
            self.assertEqual(before.lines[0], "First ")
            self.assertNotEqual(after.lines, before.lines)

    def test_wrapped_ascii_rows_map_to_their_exact_public_cells(self):
        with Shitty(columns=3, rows=4, save_lines=4) as terminal:
            terminal.write(b"ABCDEFGHIJ")
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.lines, ["ABC", "DEF", "GHI", "J  "])

            for row, expected in enumerate((b"ABC", b"DEF", b"GHI", b"J")):
                terminal.select_start(0, row)
                terminal.select_update(3, row)
                self.assertEqual(terminal.select_finish(), expected)

    def test_wide_wrap_offset_keeps_glyph_and_continuation_atomic(self):
        with Shitty(columns=3, rows=4, save_lines=4) as terminal:
            terminal.write("AB中DEF".encode())
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.lines[:3], ["AB ", "中 D", "EF "])
            self.assertTrue(snapshot.cell(0, 1).double_width)
            self.assertTrue(snapshot.cell(1, 1).double_width_continuation)

            terminal.select_start(0, 1)
            terminal.select_update(3, 1)
            self.assertEqual(terminal.select_finish(), "中D".encode())

    def test_full_ascii_wrap_count_uses_width_and_nonempty_tail(self):
        with Shitty(columns=3, rows=4, save_lines=4) as terminal:
            terminal.write(b"ABCDEFGHIJ")
            snapshot = terminal.model_snapshot()
            self.assertEqual(
                [snapshot.cell(2, row).wrapped for row in range(4)],
                [True, True, True, False],
            )
            self.assertEqual(
                [sum(snapshot.cell(column, row).drawn for column in range(3))
                 for row in range(4)],
                [3, 3, 3, 1],
            )

    def test_wide_pre_wrap_row_counts_as_a_soft_physical_segment(self):
        with Shitty(columns=3, rows=4, save_lines=4) as terminal:
            terminal.write("AB中DEF".encode())
            snapshot = terminal.model_snapshot()
            self.assertEqual(
                [snapshot.cell(1 if row == 0 else 2, row).wrapped
                 for row in range(3)],
                [True, True, False],
            )
            self.assertEqual(terminal.all_text()[:3], ("AB", "中D", "EF"))

    def test_wrapped_rows_map_back_to_their_hard_source_lines(self):
        with Shitty(columns=3, rows=5, save_lines=4) as terminal:
            terminal.write(b"One\r\nFour\r\nHello")
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.lines, ["One", "Fou", "r  ", "Hel", "lo "])
            self.assertEqual(
                [snapshot.cell(2, row).wrapped for row in range(5)],
                [False, True, False, True, False],
            )

            for begin, end, expected in (
                ((0, 0), (3, 0), b"One"),
                ((0, 1), (3, 2), b"Four"),
                ((0, 3), (3, 4), b"Hello"),
            ):
                terminal.select_start(*begin)
                terminal.select_update(*end)
                self.assertEqual(terminal.select_finish(), expected)

    def test_empty_and_nonempty_raw_lines_keep_zero_width_locations(self):
        with Shitty(columns=5, rows=6, save_lines=4) as terminal:
            terminal.write(b"\r\n\r\nA\r\n\r\nBC")
            self.assertEqual(terminal.all_text(), ("", "", "A", "", "BC", ""))
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.lines, [
                "     ",
                "     ",
                "A    ",
                "     ",
                "BC   ",
                "     ",
            ])
            self.assertTrue(all(not snapshot.cell(4, row).wrapped for row in range(6)))


if __name__ == "__main__":
    unittest.main()
