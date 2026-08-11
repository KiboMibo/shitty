# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

"""Public adaptations of the first 77 iTerm2 LineBlock cases."""

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
    "testGetPositionOfLineMapsOffsetBasic",
    "testGetPositionOfLineWrapOnEOLTrueAtHardEOL",
    "testGetPositionOfLineWrapOnEOLFalseAtHardEOL",
    "testGetPositionOfLineStartOfWrappedLineIncrementsYOffset",
    "testRandomMatchesGoldenData",
    "testFindSubstringPlainForwardSingleResult",
    "testFindSubstringPlainBackwardMultipleResults",
    "testFindSubstringRegexCaseInsensitive",
    "testFindSubstringMultiLineMode",
    "testMetadataArrayNumEntriesEqualsCLLEntriesAfterAppendAndPop",
    "testInvalidateMarksBlockInvalidated",
    "testAppendAfterRemoveLastRawLine",
    "testAppendFailsWhenExceedingMaxLines",
    "testDidFindRTLInLineSetsMetadataFlag",
    "testSizeFromLineWhenLineNotFoundReturnsZero",
    "testSizeFromLineCalculatesRemainingSpace",
    "testOffsetOfStartOfLineIncludingOffsetBeforeFirstEntry",
    "testOffsetOfStartOfLineIncludingOffsetWithinMiddleLine",
    "testOffsetOfStartOfLineIncludingOffsetOnSecondRawLineEdge",
    "testScreenCharArrayForWrappedLinePaddedToLength",
    "testRawLineNumberAtWrappedLineOffset",
    "testRawLineAtWrappedLineOffsetWithoutMetadata",
    "testRawLineWithMetadataAtWrappedLineOffsetReturnsCorrectMetadata",
    "testMetadataForRawLineAtWrappedLineOffset",
    "testReloadBidiInfoPopulatesBidiDisplayInfo",
    "testSetBidiForLastRawLineOverridesMetadata",
    "testEraseRTLStatusInAllCharactersClearsRTLStatusInChars",
    "testReloadBidiInfoIsIdempotentForUnchangedRTLContent",
    "testReloadBidiInfoReAnnotatesAfterRTLErasure",
    "testReloadBidiInfoClearsStaleRTLFoundFlag",
    "testDropMirroringProgenitorDropsLeadingLinesToMatchProgenitor",
    "testFindSubstringRegexBackwardFindsLastMatch",
    "testFindSubstringFindOneResultPerRawLine",
    "testConvertPositionDoubleWidthBranch",
    "testMutationCounterAdvancesOnAppend",
    "testMutationCounterAdvancesOnInPlaceRTLErasure",
    "testMutationCounterSurvivesCopy",
    "testMutationCounterDistinguishesDivergentCowSiblings",
    "testCanIncrementalMerge_BasicEligibility",
    "testCanIncrementalMerge_IneligibleWhenNewLineAdded",
)


class ITerm2LineBlockTest(unittest.TestCase):
    def test_upstream_inventory_has_first_77_distinct_cases(self):
        self.assertEqual(len(PORTED_CASES), 77)
        self.assertEqual(len(set(PORTED_CASES)), 77)

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

    def test_raw_offset_maps_to_the_exact_wrapped_cell(self):
        with Shitty(columns=5, rows=3, save_lines=4) as terminal:
            terminal.write(b"ABCDEFGHIJ")
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.lines, ["ABCDE", "FGHIJ", "     "])

            terminal.select_start(2, 1)
            terminal.select_update(3, 1)
            self.assertEqual(terminal.select_finish(), b"H")
            self.assertEqual(terminal.selection_state()["raw"], (2, 1, 3, 1))

    def test_hard_eol_offset_can_map_to_the_next_line_origin(self):
        with Shitty(columns=10, rows=3, save_lines=4) as terminal:
            terminal.write(b"Hello\r\n")
            snapshot = terminal.model_snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 1))

            terminal.select_start(0, 0)
            terminal.select_update(0, 1)
            self.assertEqual(terminal.select_finish(), b"Hello")
            self.assertEqual(terminal.selection_state()["raw"], (0, 0, 0, 1))

    def test_hard_eol_offset_can_stay_at_the_previous_line_end(self):
        with Shitty(columns=10, rows=3, save_lines=4) as terminal:
            terminal.write(b"Hello\r\nHello")
            terminal.select_start(0, 0)
            terminal.select_update(5, 0)
            self.assertEqual(terminal.select_finish(), b"Hello")
            self.assertEqual(terminal.selection_state()["raw"], (0, 0, 5, 0))

    def test_wrapped_line_origins_and_empty_hard_line_keep_distinct_rows(self):
        with Shitty(columns=2, rows=8, save_lines=4) as terminal:
            terminal.write(b"ABCDE\r\n\r\nABCDE")
            snapshot = terminal.model_snapshot()
            self.assertEqual(
                snapshot.lines,
                ["AB", "CD", "E ", "  ", "AB", "CD", "E ", "  "],
            )
            self.assertEqual(
                [snapshot.cell(1, row).wrapped for row in range(8)],
                [True, True, False, False, True, True, False, False],
            )
            for row, expected in enumerate((b"AB", b"CD", b"E", b"", b"AB", b"CD", b"E", b"")):
                terminal.select_start(0, row)
                terminal.select_update(2, row)
                self.assertEqual(terminal.select_finish(), expected)

    def test_deterministic_storage_churn_matches_an_independent_tail_model(self):
        state = 1

        def random_value():
            nonlocal state
            state = (6364136223846793005 * state + 1) & ((1 << 64) - 1)
            return state & 0x7fff_ffff_ffff_ffff

        payload = bytearray()
        logical_lines = []
        partial = bytearray()
        alphabet = b"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        for _ in range(200):
            length = 1 + random_value() % 20
            fragment = alphabet[:length]
            hard_eol = (random_value() & 5) > 3
            payload.extend(fragment)
            partial.extend(fragment)
            if hard_eol:
                payload.extend(b"\r\n")
                logical_lines.append(bytes(partial))
                partial.clear()
        payload.extend(b"END")
        partial.extend(b"END")
        logical_lines.append(bytes(partial))

        physical_lines = []
        for line in logical_lines:
            physical_lines.extend(
                line[offset:offset + 8].decode()
                for offset in range(0, len(line), 8)
            )
        expected_tail = tuple(physical_lines[-20:])

        with Shitty(columns=8, rows=3, save_lines=17) as terminal:
            terminal.write(bytes(payload))
            self.assertEqual(terminal.all_text(), expected_tail)
            self.assertEqual(terminal.scrollback_state()[0], 17)

            terminal.resize(8, 7)
            self.assertEqual(terminal.all_text(), expected_tail)
            self.assertEqual(terminal.scrollback_state()[0], 13)
            terminal.resize(8, 3)
            self.assertEqual(terminal.all_text(), expected_tail)
            self.assertEqual(terminal.scrollback_state()[0], 17)

    def test_plain_forward_match_has_the_expected_public_range(self):
        with Shitty(columns=20, rows=2, save_lines=4) as terminal:
            terminal.write(b"hello world\r\n")
            terminal.select_start(0, 0)
            terminal.select_update(5, 0)
            self.assertEqual(terminal.select_finish(), b"hello")
            self.assertEqual(terminal.selection_state()["raw"], (0, 0, 5, 0))

    def test_backward_matches_expose_occurrences_in_reverse_offset_order(self):
        with Shitty(columns=24, rows=2, save_lines=4) as terminal:
            terminal.write(b"foo bar foo baz foo\r\n")
            matches = []
            for offset in (16, 8, 0):
                terminal.select_start(offset, 0)
                terminal.select_update(offset + 3, 0)
                matches.append((offset, terminal.select_finish()))
            self.assertEqual(matches, [(16, b"foo"), (8, b"foo"), (0, b"foo")])

    def test_case_insensitive_anchored_pattern_covers_the_complete_line(self):
        import re

        with Shitty(columns=10, rows=2, save_lines=4) as terminal:
            terminal.write(b"FoObAr\r\n")
            terminal.select_start(0, 0)
            terminal.select_update(6, 0)
            extracted = terminal.select_finish().decode()
            self.assertIsNotNone(re.fullmatch(r"foobar", extracted, re.IGNORECASE))
            self.assertEqual(terminal.selection_state()["raw"], (0, 0, 6, 0))

    def test_multiline_search_source_preserves_the_hard_line_separator(self):
        with Shitty(columns=8, rows=3, save_lines=4) as terminal:
            terminal.write(b"foo\r\nbar\r\n")
            terminal.select_start(0, 0)
            terminal.select_update(3, 1)
            self.assertEqual(terminal.select_finish(), b"foo\nbar")
            self.assertEqual(terminal.all_text()[:2], ("foo", "bar"))

    def test_history_entry_count_tracks_append_and_height_pop(self):
        with Shitty(columns=10, rows=1, save_lines=4) as terminal:
            terminal.write(b"First\r\nSecond\r\n")
            self.assertEqual(terminal.scrollback_state()[0], 2)
            self.assertEqual(terminal.all_text(), ("First", "Second", ""))

            terminal.resize(10, 2)
            self.assertEqual(terminal.scrollback_state()[0], 1)
            self.assertEqual(terminal.all_text(), ("First", "Second", ""))
            self.assertEqual(terminal.model_snapshot().lines, ["Second    ", "          "])

    def test_mutation_invalidates_the_published_screen_snapshot(self):
        with Shitty(columns=6, rows=2, save_lines=4) as terminal:
            terminal.write(b"abc")
            before = terminal.model_snapshot()
            before_digest = terminal.model_digest()

            terminal.write(b"\x1b[2J\x1b[HXYZ")
            after = terminal.model_snapshot()
            self.assertEqual(before.lines[0], "abc   ")
            self.assertEqual(after.lines[0], "XYZ   ")
            self.assertNotEqual(terminal.model_digest(), before_digest)

    def test_cleared_last_line_accepts_a_shorter_replacement_without_stale_wrap(self):
        with Shitty(columns=4, rows=2, save_lines=8) as terminal:
            terminal.write(b"ABCDEFG\r\n")
            self.assertEqual(terminal.all_text(), ("ABCD", "EFG", ""))

            terminal.write(b"\x1b[3J\x1b[2J\x1b[HXYZ")
            snapshot = terminal.model_snapshot()
            self.assertEqual(terminal.scrollback_state()[0], 0)
            self.assertEqual(snapshot.lines, ["XYZ ", "    "])
            self.assertFalse(snapshot.cell(3, 0).wrapped)

    def test_more_than_one_line_block_limit_rolls_into_public_history(self):
        with Shitty(columns=4, rows=2, save_lines=10005) as terminal:
            terminal.write(b"\r\n" * 10001 + b"XYZ")
            self.assertEqual(terminal.scrollback_state()[0], 10000)
            self.assertEqual(len(terminal.all_text()), 10002)
            self.assertEqual(terminal.all_text()[-3:], ("", "", "XYZ"))
            self.assertEqual(
                (terminal.model_snapshot().cursor_x, terminal.model_snapshot().cursor_y),
                (3, 1),
            )

    def test_strong_rtl_text_survives_wrap_metadata_and_selection_round_trip(self):
        payload = "abc אבג".encode()
        with Shitty(columns=5, rows=4, save_lines=4) as terminal:
            terminal.write(payload)
            self.assertEqual(terminal.model_snapshot().lines[:2], ["abc א", "בג   "])
            terminal.select_start(0, 0)
            terminal.select_update(2, 1)
            self.assertEqual(terminal.select_finish(), payload)

            terminal.resize(4, 4)
            self.assertEqual(terminal.model_snapshot().lines[:2], ["abc ", "אבג "])
            terminal.select_start(0, 0)
            terminal.select_update(3, 1)
            self.assertEqual(terminal.select_finish(), payload)

    def test_missing_wrapped_line_has_zero_public_content(self):
        with Shitty(columns=5, rows=2, save_lines=4) as terminal:
            self.assertEqual(terminal.all_text(), ("", ""))
            terminal.select_start(0, 0)
            terminal.select_update(0, 0)
            self.assertEqual(terminal.select_finish(), b"")

    def test_wrapped_line_origins_expose_the_exact_remaining_suffix(self):
        with Shitty(columns=4, rows=3, save_lines=4) as terminal:
            terminal.write(b"ABCDEFGHIJ")
            self.assertEqual(terminal.model_snapshot().lines, ["ABCD", "EFGH", "IJ  "])
            for row, expected in enumerate((b"ABCDEFGHIJ", b"EFGHIJ", b"IJ")):
                terminal.select_start(0, row)
                terminal.select_update(4, 2)
                self.assertEqual(terminal.select_finish(), expected)

    def test_selection_before_first_cell_clamps_to_first_raw_line_start(self):
        with Shitty(columns=10, rows=3, save_lines=4) as terminal:
            terminal.write(b"Hello\r\nWorld")
            terminal.select_start(-5, 0)
            terminal.select_update(5, 0)
            self.assertEqual(terminal.select_finish(), b"Hello")
            self.assertEqual(terminal.selection_state()["raw"], (0, 0, 5, 0))

    def test_word_from_middle_offset_expands_to_its_raw_line_boundary(self):
        with Shitty(columns=10, rows=3, save_lines=4) as terminal:
            terminal.write(b"Hello\r\nWorld")
            result = b""
            for click in range(2):
                when = 1.0 + click * 0.1
                result = terminal.button(0, True, x=4, y=3, time=when)
                result = terminal.button(0, False, x=4, y=3, time=when + 0.01)
            self.assertEqual(result, b"World")
            self.assertEqual(terminal.selection_state()["snapped"], (0, 1, 5, 1))

    def test_exact_raw_line_boundary_does_not_include_the_previous_line(self):
        with Shitty(columns=10, rows=3, save_lines=4) as terminal:
            terminal.write(b"Hello\r\nWorld")
            terminal.select_start(0, 1)
            terminal.select_update(5, 1)
            self.assertEqual(terminal.select_finish(), b"World")
            self.assertEqual(terminal.selection_state()["raw"], (0, 1, 5, 1))

    def test_short_wrapped_line_is_padded_with_undrawn_blank_cells(self):
        with Shitty(columns=6, rows=2, save_lines=4) as terminal:
            terminal.write(b"XYZ")
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.lines[0], "XYZ   ")
            self.assertEqual(
                [snapshot.cell(column, 0).drawn for column in range(6)],
                [True, True, True, False, False, False],
            )
            self.assertEqual(
                [snapshot.cell(column, 0).char for column in range(3, 6)],
                [" ", " ", " "],
            )

    def test_wrapped_row_indices_map_to_their_hard_logical_lines(self):
        with Shitty(columns=3, rows=4, save_lines=4) as terminal:
            terminal.write(b"ABC\r\nDEFG")
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.lines, ["ABC", "DEF", "G  ", "   "])
            self.assertEqual(
                [snapshot.cell(2, row).wrapped for row in range(4)],
                [False, True, False, False],
            )
            terminal.select_start(0, 1)
            terminal.select_update(1, 2)
            self.assertEqual(terminal.select_finish(), b"DEFG")

    def test_each_wrapped_segment_maps_back_to_the_complete_raw_line(self):
        with Shitty(columns=2, rows=6, save_lines=4) as terminal:
            terminal.write(b"One\r\nTwoTwo")
            self.assertEqual(
                terminal.model_snapshot().lines,
                ["On", "e ", "Tw", "oT", "wo", "  "],
            )
            for row in (0, 1):
                terminal.select_start(0, row)
                terminal.select_extend(0, row, cycle=True)
                terminal.select_extend(0, row, cycle=True)
                self.assertEqual(terminal.select_finish(), b"One")
            for row in (2, 3, 4):
                terminal.select_start(0, row)
                terminal.select_extend(0, row, cycle=True)
                terminal.select_extend(0, row, cycle=True)
                self.assertEqual(terminal.select_finish(), b"TwoTwo")

    def test_wrapped_raw_line_preserves_its_hyperlink_metadata(self):
        first_uri = "https://example.com/first"
        second_uri = "https://example.com/second"
        with Shitty(columns=5, rows=5, save_lines=4) as terminal:
            terminal.write(
                b"\x1b]8;;https://example.com/first\x1b\\FirstLine\x1b]8;;\x1b\\\r\n"
                b"\x1b]8;;https://example.com/second\x1b\\SecondLine\x1b]8;;\x1b\\"
            )
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.lines[:4], ["First", "Line ", "Secon", "dLine"])
            for column, row in ((0, 0), (4, 0), (0, 1), (3, 1)):
                self.assertEqual(terminal.hyperlink(column, row), first_uri)
            for column, row in ((0, 2), (4, 2), (0, 3), (4, 3)):
                self.assertEqual(terminal.hyperlink(column, row), second_uri)
            self.assertEqual(terminal.hyperlink(4, 1), "")

    def test_semantic_metadata_is_shared_by_every_wrap_of_its_raw_line(self):
        with Shitty(columns=4, rows=4, save_lines=4) as terminal:
            terminal.write(
                b"\x1b]133;A\x1b\\ABCDEFG"
                b"\x1b]133;B\x1b\\\x1b]133;D\x1b\\\r\nXYZ"
            )
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.lines, ["ABCD", "EFG ", "XYZ ", "    "])
            self.assertEqual(
                [snapshot.cell(column, 0).semantic for column in range(4)]
                + [snapshot.cell(column, 1).semantic for column in range(3)],
                [1] * 7,
            )
            self.assertTrue(all(snapshot.cell(column, 2).semantic == 0 for column in range(3)))

    def test_strong_rtl_line_populates_stable_public_logical_text(self):
        payload = "אבג".encode()
        with Shitty(columns=3, rows=3, save_lines=4) as terminal:
            terminal.write(payload)
            terminal.select_start(0, 0)
            terminal.select_update(3, 0)
            self.assertEqual(terminal.select_finish(), payload)
            self.assertEqual(terminal.all_text()[0], "אבג")

    def test_rtl_reflow_can_replace_private_bidi_metadata_without_text_loss(self):
        payload = "אבגדה".encode()
        with Shitty(columns=5, rows=3, save_lines=4) as terminal:
            terminal.write(payload)
            terminal.resize(3, 3)
            self.assertEqual(terminal.model_snapshot().lines[:2], ["אבג", "דה "])
            terminal.resize(5, 3)
            self.assertEqual(terminal.model_snapshot().lines[0], "אבגדה")
            terminal.select_start(0, 0)
            terminal.select_update(5, 0)
            self.assertEqual(terminal.select_finish(), payload)

    def test_erasing_rtl_cells_clears_their_public_cell_state(self):
        with Shitty(columns=5, rows=2, save_lines=4) as terminal:
            terminal.write("אבג".encode())
            terminal.write(b"\x1b[2J\x1b[H")
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.lines, ["     ", "     "])
            self.assertTrue(all(not cell.drawn and not cell.grapheme for cell in snapshot.cells))

    def test_repeated_rtl_observation_is_idempotent(self):
        with Shitty(columns=5, rows=2, save_lines=4) as terminal:
            terminal.write("אבג".encode())
            before = terminal.model_digest()
            first = terminal.model_snapshot()
            second = terminal.model_snapshot()
            self.assertEqual(second.lines, first.lines)
            self.assertEqual(terminal.model_digest(), before)

    def test_rewriting_rtl_after_erasure_reannotates_public_cells(self):
        payload = "אבג".encode()
        with Shitty(columns=5, rows=2, save_lines=4) as terminal:
            terminal.write(payload + b"\x1b[2J\x1b[H" + payload)
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.lines[0], "אבג  ")
            terminal.select_start(0, 0)
            terminal.select_update(3, 0)
            self.assertEqual(terminal.select_finish(), payload)
            self.assertTrue(all(snapshot.cell(column, 0).drawn for column in range(3)))

    def test_ltr_replacement_does_not_retain_stale_rtl_state(self):
        with Shitty(columns=5, rows=2, save_lines=4) as reused, Shitty(
            columns=5, rows=2, save_lines=4
        ) as fresh:
            reused.write("אבג".encode() + b"\x1b[2J\x1b[Habc")
            fresh.write(b"abc")
            self.assertEqual(reused.model_digest(), fresh.model_digest())
            self.assertEqual(reused.model_snapshot().lines, fresh.model_snapshot().lines)

    def test_old_snapshot_survives_leading_history_drop(self):
        with Shitty(columns=5, rows=2, save_lines=2) as terminal:
            terminal.write(b"one\r\ntwo\r\n")
            old = terminal.model_snapshot()
            terminal.write(b"three\r\nfour\r\n")
            self.assertEqual(old.lines, ["two  ", "     "])
            self.assertEqual(terminal.all_text(), ("two", "three", "four", ""))
            self.assertEqual(terminal.scrollback_state()[0], 2)

    def test_backward_regex_selects_the_rightmost_match(self):
        import re

        with Shitty(columns=20, rows=2, save_lines=4) as terminal:
            terminal.write(b"item1 item2 item3")
            text = terminal.all_text()[0]
            matches = list(re.finditer(r"item\d", text))
            self.assertEqual([(match.start(), match.group()) for match in matches], [
                (0, "item1"),
                (6, "item2"),
                (12, "item3"),
            ])
            terminal.select_start(matches[-1].start(), 0)
            terminal.select_update(matches[-1].end(), 0)
            self.assertEqual(terminal.select_finish(), b"item3")

    def test_first_match_per_raw_line_has_independent_public_ranges(self):
        with Shitty(columns=10, rows=3, save_lines=4) as terminal:
            terminal.write(b"foo X foo\r\nfoo Y foo")
            self.assertEqual(terminal.all_text()[:2], ("foo X foo", "foo Y foo"))
            for row in (0, 1):
                terminal.select_start(0, row)
                terminal.select_update(3, row)
                self.assertEqual(terminal.select_finish(), b"foo")
                self.assertEqual(terminal.selection_state()["raw"], (0, row, 3, row))

    def test_double_width_offset_maps_to_the_visible_cell_coordinates(self):
        with Shitty(columns=2, rows=4, save_lines=4) as terminal:
            terminal.write("A中BC".encode())
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.lines, ["A ", "中 ", "BC", "  "])
            self.assertTrue(snapshot.cell(0, 1).double_width)
            self.assertTrue(snapshot.cell(1, 1).double_width_continuation)
            terminal.select_start(0, 1)
            terminal.select_update(1, 2)
            self.assertEqual(terminal.select_finish(), "中B".encode())

    def test_public_model_identity_advances_on_append(self):
        with Shitty(columns=5, rows=2, save_lines=4) as terminal:
            before = terminal.model_digest()
            terminal.write(b"abc")
            self.assertNotEqual(terminal.model_digest(), before)
            self.assertEqual(terminal.model_snapshot().lines[0], "abc  ")

    def test_rtl_erasure_advances_public_model_identity(self):
        with Shitty(columns=5, rows=2, save_lines=4) as terminal:
            terminal.write("אבג".encode())
            before = terminal.model_digest()
            terminal.write(b"\x1b[2J")
            self.assertNotEqual(terminal.model_digest(), before)
            self.assertEqual(terminal.last_update_rows(), (0, 1))

    def test_published_snapshot_keeps_its_identity_after_later_mutation(self):
        with Shitty(columns=5, rows=2, save_lines=4) as terminal:
            terminal.write(b"abc")
            published = terminal.model_snapshot()
            terminal.write(b"def")
            self.assertEqual(published.lines[0], "abc  ")
            self.assertEqual(terminal.model_snapshot().lines[:2], ["abcde", "f    "])

    def test_divergent_public_copies_have_distinct_model_identities(self):
        with Shitty(columns=5, rows=2, save_lines=4) as left, Shitty(
            columns=5, rows=2, save_lines=4
        ) as right:
            left.write(b"abc")
            right.write(b"abc")
            self.assertEqual(left.model_digest(), right.model_digest())
            left.write(b"L")
            right.write(b"R")
            self.assertNotEqual(left.model_digest(), right.model_digest())
            self.assertEqual(left.model_snapshot().lines[0], "abcL ")
            self.assertEqual(right.model_snapshot().lines[0], "abcR ")

    def test_partial_append_can_incrementally_extend_a_published_line(self):
        with Shitty(columns=8, rows=2, save_lines=4) as terminal:
            terminal.write(b"abc")
            published = terminal.model_snapshot()
            terminal.write(b"def")
            self.assertEqual(published.lines[0], "abc     ")
            self.assertEqual(terminal.model_snapshot().lines[0], "abcdef  ")
            self.assertEqual((terminal.model_snapshot().cursor_x, terminal.model_snapshot().cursor_y), (6, 0))

    def test_hard_line_append_is_not_incrementally_merged(self):
        with Shitty(columns=8, rows=3, save_lines=4) as terminal:
            terminal.write(b"abc\r\ndef")
            self.assertEqual(terminal.model_snapshot().lines, ["abc     ", "def     ", "        "])
            terminal.select_start(0, 0)
            terminal.select_update(3, 1)
            self.assertEqual(terminal.select_finish(), b"abc\ndef")


if __name__ == "__main__":
    unittest.main()
