# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

"""Public adaptations of the first 20 iTerm2 LineBuffer cases."""

import unittest

from harness import Shitty, put_rows


PORTED_CASES = (
    "testBasic",
    "testBasic_Wraps",
    "testCopyOnWrite_ModifySecond",
    "testCopyOnWrite_ModifyFirst",
    "testCopyOnWrite_ModifyBoth",
    "testCopyOnWrite_CopyOfCopy",
    "testCopyOnWrite_ClientKeepsOwnerAliveUntilWriteToSecond",
    "testCopyOnWrite_ClientKeepsOwnerAliveUntilWriteToFirst",
    "testCopyOnWrite_Pop",
    "testCopyOnWrite_Truncate",
    "testDropExcessLinesAdjustsCursorXWhenItsRawLineIsPartiallyTrimmed",
    "testConvertPositionMultiBlock",
    "testCoordinateForPosition_extendsToEOL_yOffset0_extendsRight",
    "testCoordinateForPosition_extendsToEOL_yOffset0_notExtendsRight",
    "testCoordinateForPosition_extendsToEOL_yOffsetPositive_extendsRight",
    "testCoordinateForPosition_extendsToEOL_yOffsetPositive_notExtendsRight",
    "testRoundTrip_singleLine_noWrap",
    "testRoundTrip_pastEOL_collapsesToEndOfContent",
    "testRoundTrip_pastEOL_extendsRight_clampsToWidthMinus1",
    "testRoundTrip_wrappedHardEOLLine",
    "testRoundTrip_softEOL",
    "testRoundTrip_emptyLinesInMiddle",
    "testRoundTrip_multiBlock_threeBlocks",
    "testRoundTrip_multiBlock",
    "testRoundTrip_widthOne",
    "testCrossWidth_wideToNarrow",
    "testCrossWidth_narrowToWide",
    "testRoundTrip_lastPositionPastEOL_extendsRight",
    "testRoundTrip_lastPositionPastEOL_noExtendsRight",
    "testRoundTrip_origin",
    "testRoundTrip_lineLengthEqualsWidth",
    "testRoundTrip_exhaustiveMixed",
    "testCrossWidth_pastEOL_collapsesToEndOfContent",
    "testRoundTrip_positionStableAcrossForceSeal",
    "testFirstAndLastPosition_mapToBoundaryCoords",
    "testBlockContaining_singleBlock_middleOfContent",
    "testBlockContaining_singleBlock_atEnd",
    "testBlockContaining_twoBlocks_atBoundary_secondLonger",
    "testBlockContaining_threeBlocks_atInnerBoundary",
    "testBlockContaining_threeBlocks_atInnerBoundary_nextBlockShorter",
)


class ITerm2LineBufferTest(unittest.TestCase):
    def test_upstream_inventory_has_first_40_distinct_cases(self):
        self.assertEqual(len(PORTED_CASES), 40)
        self.assertEqual(len(set(PORTED_CASES)), 40)

    def test_basic_keeps_two_hard_lines_in_order(self):
        with Shitty(columns=80, rows=3, save_lines=4) as terminal:
            terminal.write(b"Hello world\r\nGoodbye cruel world\r\n")
            snapshot = terminal.model_snapshot()
            self.assertEqual(
                tuple(line.rstrip() for line in snapshot.lines),
                ("Hello world", "Goodbye cruel world", ""),
            )
            self.assertFalse(snapshot.cell(79, 0).wrapped)
            self.assertFalse(snapshot.cell(79, 1).wrapped)

    def test_basic_wraps_into_the_eight_expected_physical_rows(self):
        with Shitty(columns=4, rows=8, save_lines=0) as terminal:
            terminal.write(b"Hello world\r\nGoodbye cruel world")
            snapshot = terminal.model_snapshot()
            self.assertEqual(
                snapshot.lines,
                ["Hell", "o wo", "rld ", "Good", "bye ", "crue", "l wo", "rld "],
            )
            self.assertEqual(
                [snapshot.cell(3, row).wrapped for row in range(8)],
                [True, True, False, True, True, True, True, False],
            )

    def test_modify_second_replay_leaves_the_first_session_unchanged(self):
        with Shitty(columns=24, rows=3, save_lines=4) as terminal:
            terminal.write(b"Hello world")
            terminal.new_session()
            terminal.write(b"Hello world\r\nGoodbye cruel world")
            self.assertEqual(
                terminal.all_text()[:2],
                ("Hello world", "Goodbye cruel world"),
            )
            terminal.chord_prev_tab()
            self.assertEqual(terminal.all_text()[0], "Hello world")
            self.assertEqual(tuple(filter(None, terminal.all_text())), ("Hello world",))

    def test_modify_first_replay_leaves_the_second_session_unchanged(self):
        with Shitty(columns=24, rows=3, save_lines=4) as terminal:
            terminal.write(b"Hello world")
            terminal.new_session()
            terminal.write(b"Hello world")
            terminal.write_to(0, b"\r\nGoodbye cruel world")
            self.assertEqual(tuple(filter(None, terminal.all_text())), ("Hello world",))
            terminal.chord_prev_tab()
            self.assertEqual(
                terminal.all_text()[:2],
                ("Hello world", "Goodbye cruel world"),
            )

    def test_modify_both_replays_diverge_without_cross_session_aliasing(self):
        with Shitty(columns=24, rows=3, save_lines=4) as terminal:
            terminal.write(b"Hello world")
            terminal.new_session()
            terminal.write(b"Hello world\r\nHello world")
            terminal.write_to(0, b"\r\nGoodbye cruel world")
            self.assertEqual(terminal.all_text()[:2], ("Hello world", "Hello world"))
            terminal.chord_prev_tab()
            self.assertEqual(
                terminal.all_text()[:2],
                ("Hello world", "Goodbye cruel world"),
            )

    def test_copy_of_copy_replays_three_independently_diverging_states(self):
        with Shitty(columns=24, rows=3, save_lines=4) as terminal:
            terminal.write(b"Hello world")
            terminal.new_session()
            terminal.write(b"Hello world\r\nGoodbye cruel world")
            terminal.new_session()
            terminal.write(b"Hello world\r\nI like traffic lights")

            expected = (
                ("Hello world", "I like traffic lights"),
                ("Hello world", "Goodbye cruel world"),
                ("Hello world",),
            )
            for lines in expected:
                self.assertEqual(tuple(filter(None, terminal.all_text())), lines)
                terminal.chord_prev_tab()

    def test_surviving_second_session_remains_writable_after_first_is_destroyed(self):
        with Shitty(columns=24, rows=2, save_lines=4) as terminal:
            terminal.write(b"Hello world")
            terminal.new_session()
            terminal.write(b"Hello world")
            terminal.close_session(0)
            self.assertEqual(terminal.session_state(), (1, 0))
            terminal.write(b"\r\nGoodbye cruel world")
            self.assertEqual(
                terminal.all_text(),
                ("Hello world", "Goodbye cruel world"),
            )

    def test_surviving_first_session_remains_writable_after_second_is_destroyed(self):
        with Shitty(columns=24, rows=2, save_lines=4) as terminal:
            terminal.write(b"Hello world")
            terminal.new_session()
            terminal.write(b"Hello world")
            terminal.close_session(1)
            self.assertEqual(terminal.session_state(), (1, 0))
            terminal.write(b"\r\nGoodbye cruel world")
            self.assertEqual(
                terminal.all_text(),
                ("Hello world", "Goodbye cruel world"),
            )

    def test_height_growth_pops_the_newest_history_row_without_mutating_snapshot(self):
        with Shitty(columns=24, rows=1, save_lines=4) as terminal:
            terminal.write(b"Hello world\r\nGoodbye cruel world\r\n")
            before = terminal.model_snapshot()
            self.assertEqual(terminal.scrollback_state()[0], 2)

            terminal.resize(24, 2)
            after = terminal.model_snapshot()
            self.assertEqual(before.lines, [" " * 24])
            self.assertEqual(
                tuple(line.rstrip() for line in after.lines),
                ("Goodbye cruel world", ""),
            )
            self.assertEqual(terminal.scrollback_state()[0], 1)

    def test_bounded_truncate_changes_only_the_overflowing_session(self):
        with Shitty(columns=24, rows=1, save_lines=2) as terminal:
            terminal.write(b"Hello world\r\nGoodbye cruel world\r\n")
            terminal.new_session()
            terminal.write(b"Hello world\r\nGoodbye cruel world\r\n")
            terminal.write(b"third\r\nfourth\r\n")
            self.assertEqual(terminal.scrollback_state()[0], 2)
            self.assertEqual(terminal.all_text(), ("third", "fourth", ""))

            terminal.chord_prev_tab()
            self.assertEqual(terminal.scrollback_state()[0], 2)
            self.assertEqual(
                terminal.all_text(),
                ("Hello world", "Goodbye cruel world", ""),
            )

    def test_partial_history_trim_keeps_cursor_on_the_surviving_wrapped_suffix(self):
        wide = "界".encode()
        with Shitty(
            columns=4,
            rows=2,
            save_lines=2,
            extra_arguments=("-unicodeWidths", "17"),
        ) as terminal:
            terminal.write(b"ABCDEFGH\r\nIJK" + wide + b"NOP")
            terminal.write(b"\x1b[1;1H")
            terminal.resize(4, 4)
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.lines, ["EFGH", "IJK ", "界 NO", "P   "])
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 2))
            self.assertTrue(snapshot.cell(0, 2).double_width)
            self.assertTrue(snapshot.cell(1, 2).double_width_continuation)

    def test_coordinates_remain_addressable_across_multiple_storage_pages(self):
        payload = b"A" * 10_000
        with Shitty(columns=80, rows=4, save_lines=128) as terminal:
            terminal.write(payload + b"\r\nSecond")
            self.assertEqual("".join(terminal.all_text()).rstrip(), payload.decode() + "Second")

            terminal.wheel_up(10_000)
            terminal.select_start(0, 0)
            terminal.select_update(3, 0)
            self.assertEqual(terminal.select_finish(), b"AAA")
            terminal.wheel_down(10_000)
            terminal.select_start(0, 3)
            terminal.select_update(6, 3)
            self.assertEqual(terminal.select_finish(), b"Second")

    def test_selection_end_past_hard_eol_extends_to_the_right_edge_semantically(self):
        with Shitty(columns=10, rows=2, save_lines=0) as terminal:
            terminal.write(b"abcde\r\nxyz")
            terminal.select_start(0, 0)
            terminal.select_update(10, 0)
            self.assertEqual(terminal.select_finish(), b"abcde")

    def test_selection_start_past_hard_eol_collapses_to_natural_content_end(self):
        with Shitty(columns=10, rows=2, save_lines=0) as terminal:
            terminal.write(b"abcde\r\nxyz")
            terminal.select_start(7, 0)
            terminal.select_update(3, 1)
            self.assertEqual(terminal.select_finish(), b"\nxyz")

    def test_selection_end_on_a_later_empty_row_extends_to_its_right_edge(self):
        with Shitty(columns=10, rows=5, save_lines=0) as terminal:
            terminal.write(b"abcde\r\n\r\n\r\n\r\nxyz")
            terminal.select_start(0, 0)
            terminal.select_update(10, 2)
            self.assertEqual(terminal.snapshot().selection, (0, 0, 10, 2))

    def test_selection_start_on_a_later_empty_row_uses_column_zero(self):
        with Shitty(columns=10, rows=5, save_lines=0) as terminal:
            terminal.write(b"abcde\r\n\r\n\r\n\r\nxyz")
            terminal.select_start(7, 2)
            terminal.select_update(3, 4)
            self.assertEqual(terminal.select_finish(), b"\n\nxyz")

    def test_content_coordinates_round_trip_one_cell_at_a_time(self):
        with Shitty(columns=10, rows=2, save_lines=0) as terminal:
            terminal.write(b"abcde\r\nxyz")
            for row, text in enumerate((b"abcde", b"xyz")):
                for column, byte in enumerate(text):
                    terminal.select_start(column, row)
                    terminal.select_update(column + 1, row)
                    self.assertEqual(terminal.select_finish(), bytes((byte,)))

    def test_past_eol_selection_start_loses_the_original_blank_column(self):
        with Shitty(columns=10, rows=2, save_lines=0) as terminal:
            terminal.write(b"abcde\r\nxyz")
            results = []
            for column in (6, 7, 9):
                terminal.select_start(column, 0)
                terminal.select_update(1, 1)
                results.append(terminal.select_finish())
            self.assertEqual(results, [b"\nx"] * 3)

    def test_past_eol_selection_end_clamps_to_the_same_last_grid_column(self):
        with Shitty(columns=10, rows=2, save_lines=0) as terminal:
            terminal.write(b"abcde\r\nxyz")
            results = []
            for column in (6, 7, 9, 10):
                terminal.select_start(0, 0)
                terminal.select_update(column, 0)
                results.append(terminal.select_finish())
            self.assertEqual(results, [b"abcde"] * 4)

    def test_wrapped_hard_line_coordinates_cross_every_soft_boundary(self):
        with Shitty(columns=5, rows=4, save_lines=0) as terminal:
            terminal.write(b"Hello world\r\n")
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.lines[:3], ["Hello", " worl", "d    "])
            self.assertEqual(
                [snapshot.cell(4, row).wrapped for row in range(3)],
                [True, True, False],
            )
            terminal.select_start(0, 0)
            terminal.select_update(1, 2)
            self.assertEqual(terminal.select_finish(), b"Hello world")

    def test_soft_eol_appends_form_one_wrapped_logical_line(self):
        with Shitty(columns=5, rows=2, save_lines=0) as terminal:
            terminal.write(b"abcdefghij")
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.lines, ["abcde", "fghij"])
            self.assertTrue(snapshot.cell(4, 0).wrapped)
            self.assertFalse(snapshot.cell(4, 1).wrapped)
            terminal.select_start(0, 0)
            terminal.select_update(5, 1)
            self.assertEqual(terminal.select_finish(), b"abcdefghij")

    def test_empty_hard_lines_in_the_middle_keep_distinct_coordinates(self):
        with Shitty(columns=10, rows=4, save_lines=0) as terminal:
            terminal.write(put_rows(b"abc", b"", b"", b"xyz"))
            self.assertEqual(terminal.all_text(), ("abc", "", "", "xyz"))
            for row in (1, 2):
                terminal.select_start(0, row)
                terminal.select_update(1, row)
                self.assertEqual(terminal.select_finish(), b"")
            terminal.select_start(0, 3)
            terminal.select_update(3, 3)
            self.assertEqual(terminal.select_finish(), b"xyz")

    def test_three_successive_storage_epochs_keep_every_old_coordinate_live(self):
        with Shitty(columns=10, rows=3, save_lines=0) as terminal:
            terminal.write(b"first")
            first = terminal.model_snapshot()
            terminal.write(b"\r\nsecond")
            second = terminal.model_snapshot()
            terminal.write(b"\r\nthird")

            self.assertEqual(first.lines, ["first     ", "          ", "          "])
            self.assertEqual(second.lines[:2], ["first     ", "second    "])
            self.assertEqual(terminal.all_text(), ("first", "second", "third"))
            for row, word in enumerate((b"first", b"second", b"third")):
                terminal.select_start(0, row)
                terminal.select_update(len(word), row)
                self.assertEqual(terminal.select_finish(), word)

    def test_two_storage_epochs_join_at_one_public_hard_line_boundary(self):
        with Shitty(columns=10, rows=2, save_lines=0) as terminal:
            terminal.write(b"first")
            before = terminal.model_snapshot()
            terminal.write(b"\r\nsecond")
            terminal.select_start(0, 0)
            terminal.select_update(6, 1)
            self.assertEqual(terminal.select_finish(), b"first\nsecond")
            self.assertEqual(before.lines[0], "first     ")

    def test_width_one_gives_each_character_its_own_wrapped_row(self):
        with Shitty(columns=1, rows=3, save_lines=0) as terminal:
            terminal.write(b"abc")
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.lines, ["a", "b", "c"])
            self.assertEqual(
                [snapshot.cell(0, row).wrapped for row in range(3)],
                [True, True, False],
            )

    def test_cursor_position_maps_from_wide_to_narrow_reflow(self):
        with Shitty(columns=20, rows=3, save_lines=4) as terminal:
            terminal.write(b"abcdefghij\x1b[1;7H")
            self.assertEqual(
                (terminal.snapshot().cursor_x, terminal.snapshot().cursor_y),
                (6, 0),
            )
            terminal.resize(4, 3)
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.lines, ["abcd", "efgh", "ij  "])
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (2, 1))

    def test_cursor_position_maps_from_narrow_to_wide_reflow(self):
        with Shitty(columns=4, rows=3, save_lines=4) as terminal:
            terminal.write(b"abcdefghij\x1b[2;3H")
            self.assertEqual(
                (terminal.snapshot().cursor_x, terminal.snapshot().cursor_y),
                (2, 1),
            )
            terminal.resize(20, 3)
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.lines[0], "abcdefghij" + " " * 10)
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (6, 0))

    def test_last_line_end_can_extend_to_the_right_grid_boundary(self):
        with Shitty(columns=10, rows=2, save_lines=0) as terminal:
            terminal.write(b"abc\r\nxyz")
            terminal.select_start(0, 1)
            terminal.select_update(10, 1)
            self.assertEqual(terminal.snapshot().selection, (0, 1, 10, 1))
            self.assertEqual(terminal.select_finish(), b"xyz")

    def test_last_line_end_can_remain_at_the_natural_content_boundary(self):
        with Shitty(columns=10, rows=2, save_lines=0) as terminal:
            terminal.write(b"abc\r\nxyz")
            terminal.select_start(0, 1)
            terminal.select_update(3, 1)
            self.assertEqual(terminal.snapshot().selection, (0, 1, 3, 1))
            self.assertEqual(terminal.select_finish(), b"xyz")

    def test_origin_round_trips_as_the_first_content_cell(self):
        with Shitty(columns=10, rows=1, save_lines=0) as terminal:
            terminal.write(b"abc")
            terminal.select_start(0, 0)
            terminal.select_update(1, 0)
            self.assertEqual(terminal.select_finish(), b"a")

    def test_exact_width_hard_line_has_no_phantom_continuation_row(self):
        with Shitty(columns=5, rows=2, save_lines=0) as terminal:
            terminal.write(b"abcde\r\nxyz")
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.lines, ["abcde", "xyz  "])
            self.assertFalse(snapshot.cell(4, 0).wrapped)
            for column, byte in enumerate(b"abcde"):
                terminal.select_start(column, 0)
                terminal.select_update(column + 1, 0)
                self.assertEqual(terminal.select_finish(), bytes((byte,)))

    def test_mixed_hard_soft_and_empty_rows_have_exhaustive_cell_coordinates(self):
        logical = ("Hell", "o wo", "rld", "ABC", "", "1234", "5678", "z")
        with Shitty(columns=4, rows=8, save_lines=0) as terminal:
            terminal.write(b"Hello world\r\nABC\r\n\r\n12345678\r\nz")
            self.assertEqual(terminal.all_text(), logical)
            for row, text in enumerate(logical):
                for column, char in enumerate(text.encode()):
                    terminal.select_start(column, row)
                    terminal.select_update(column + 1, row)
                    self.assertEqual(terminal.select_finish(), bytes((char,)))

    @unittest.expectedFailure
    def test_past_eol_anchor_uses_iterm2_collapse_policy_across_widths(self):
        with Shitty(columns=20, rows=5, save_lines=4) as terminal:
            terminal.write(b"abcdefghij\r\nzzz\x1b[1;16H")
            terminal.resize(4, 5)
            snapshot = terminal.model_snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (2, 2))
            self.assertEqual(terminal.all_text(), ("abcd", "efgh", "ij", "zzz", ""))

    def test_selection_position_stays_live_after_later_lines_are_appended(self):
        with Shitty(columns=10, rows=2, save_lines=4) as terminal:
            terminal.write(b"abcde")
            terminal.select_start(2, 0)
            terminal.select_update(3, 0)
            terminal.write(b"\r\nxyz")
            self.assertEqual(terminal.select_finish(), b"c")

    def test_first_and_last_public_boundaries_cover_the_whole_buffer(self):
        with Shitty(columns=10, rows=2, save_lines=0) as terminal:
            terminal.write(b"abc\r\nxyz")
            terminal.select_start(0, 0)
            terminal.select_update(3, 1)
            self.assertEqual(terminal.snapshot().selection, (0, 0, 3, 1))
            self.assertEqual(terminal.select_finish(), b"abc\nxyz")

    def test_single_line_middle_position_selects_the_expected_cell(self):
        with Shitty(columns=10, rows=1, save_lines=0) as terminal:
            terminal.write(b"abcde")
            terminal.select_start(2, 0)
            terminal.select_update(3, 0)
            self.assertEqual(terminal.select_finish(), b"c")

    def test_single_line_end_position_stays_on_that_hard_line(self):
        with Shitty(columns=10, rows=1, save_lines=0) as terminal:
            terminal.write(b"abcde")
            terminal.select_start(0, 0)
            terminal.select_update(5, 0)
            self.assertEqual(terminal.snapshot().selection, (0, 0, 5, 0))
            self.assertEqual(terminal.select_finish(), b"abcde")

    def test_two_line_boundary_belongs_to_the_previous_hard_line_end(self):
        with Shitty(columns=80, rows=2, save_lines=0) as terminal:
            terminal.write(b"Hello world\r\nGoodbye cruel world")
            terminal.select_start(0, 0)
            terminal.select_update(11, 0)
            self.assertEqual(terminal.snapshot().selection, (0, 0, 11, 0))
            self.assertEqual(terminal.select_finish(), b"Hello world")

    def test_three_line_inner_boundary_keeps_the_middle_line_endpoint(self):
        with Shitty(columns=10, rows=3, save_lines=0) as terminal:
            terminal.write(b"first\r\nsecond\r\nthird")
            terminal.select_start(0, 1)
            terminal.select_update(6, 1)
            self.assertEqual(terminal.snapshot().selection, (0, 1, 6, 1))
            self.assertEqual(terminal.select_finish(), b"second")

    def test_inner_boundary_is_stable_when_the_following_line_is_shorter(self):
        with Shitty(columns=10, rows=3, save_lines=0) as terminal:
            terminal.write(b"aaaa\r\nbbbbbbb\r\ncc")
            terminal.select_start(0, 1)
            terminal.select_update(7, 1)
            self.assertEqual(terminal.snapshot().selection, (0, 1, 7, 1))
            self.assertEqual(terminal.select_finish(), b"bbbbbbb")


if __name__ == "__main__":
    unittest.main()
