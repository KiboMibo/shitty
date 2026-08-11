# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

"""Public adaptations of the first 60 iTerm2 VT100Grid cases."""

import unittest

from harness import Shitty, put_rows


PORTED_CASES = (
    "testAppendLineToLineBuffer",
    "testAppendLinesWithContinuationMarks",
    "testAppendSoftContinuationThenHardEolJoinsIntoOneLine",
    "testAppendLinesRespectsCursorPosition",
    "testCursorHoistedFromBlankLineAfterSoftEOL",
    "testLengthOfLineNumber",
    "testMoveCursorDownOneLineNoScroll",
    "testMoveCursorDownOneLineBelowScrollRegionButAboveLastLine",
    "testMoveCursorDownOneLineWholeScreenScrolls",
    "testWholeScreenScrollRespectsSoftEOLs",
    "testScrollRegionFullWidthAtTopScrollsOnlyRegion",
    "testMoveCursorDownOneLineRegionScrollWithoutScrollback",
    "testWholeScreenScrollWithMaxLinesDropsOldLines",
    "testScrollRegionWithRowsAndColsScrollsWithinRegionOnly",
    "testMoveCursorLeft_DefaultBehavior",
    "testMoveCursorLeft_AtScrollRegionLeftEdge_DoesNotMove",
    "testMoveCursorLeft_WithinScrollRegion_MovesLeft",
    "testMoveCursorLeft_OutsideScrollRegion_MovesNormally",
    "testMoveCursorLeftWrappingAroundSoftEOL",
    "testMoveCursorLeftWrappingAroundDoubleWideCharEOL",
    "testMoveCursorLeftNotWrappingAroundHardEOL",
    "testMoveCursorRight_DefaultBehavior",
    "testMoveCursorRight_WithScrollRegion_NoScrollbackBeforeRegion",
    "testMoveCursorRight_WithScrollRegion_EnteringRegion",
    "testMoveCursorRight_WithScrollRegion_AtRegionEnd_NoMove",
    "testMoveCursorUp_DefaultBehavior",
    "testMoveCursorUp_ClampsAtScrollRegionTop",
    "testMoveCursorUp_AboveScrollTop_DoesNotClamp",
    "testMoveCursorDown_DefaultBehavior",
    "testMoveCursorDown_ClampsAtScrollRegionBottom",
    "testMoveCursorDown_BelowScrollRegion_DoesNotClamp",
    "testScrollUpIntoLineBuffer",
    "testScrollUpIntoLineBuffer_DroppedLinesCount",
    "testScrollUpIntoLineBuffer_HorizontalRegion_NoScrollback",
    "testScrollWholeScreenUpIntoLineBuffer_DroppedLinesCountAndContent",
    "testScrollRectDownBy_ZeroDoesNothingAndMarksAllCellsDirty",
    "testScrollRectDownBy_One_MarksRegionLinesDirty",
    "testScrollRectDownBy_NegativeOne_MarksRegionLinesDirty",
    "testScrollRectDownBy_Two_MarksRegionLinesDirty",
    "testScrollRectDownBy_NegativeTwo_MarksRegionLinesDirty",
    "testScrollRectDownBy_Height_EqualsRegionHeight_MarksRegionLinesDirty",
    "testScrollRectDownBy_NegativeHeight_EqualsRegionHeight_MarksRegionLinesDirty",
    "testScrollRectDownBy_GreaterThanRegionHeight_MarksRegionLinesDirty",
    "testScrollRectDownBy_NegativeGreaterThanRegionHeight_MarksRegionLinesDirty",
    "testScrollRectDownBy_NegativeOne_CleansUpBrokenSplitDwc",
    "testScrollRectDownBy_One_CleansSplitDWCAtTop",
    "testScrollRectDownBy_NegativeOne_FullRegion_CleansSplitDwc",
    "testScrollRectDownBy_NegativeOne_CleansUpOrphanedSplitDWCAndMarksDirty",
    "testScrollRectDownBy_NegativeOne_EdgeCaseSplitDwcOrphans",
    "testScrollRectDownBy_NegativeOne_RegionFromCol1ToRightMargin_CleansSplitDwcAndMarksDirty",
    "testScrollRectDownBy_NegativeOneEdgeCaseOrphans",
    "testScrollRectDownBy_EmptyRectIsHarmless",
    "testScrollRectDownBy_NegativeOne_MoveOneDwcReplaced",
    "testScrollRectDownBy_MoveContinuationMarkToEdgeOfRect",
    "testScrollRectDownBy_MoveContinuationMarkToEdgeOfRect_ScrollUp",
    "testScrollRectDownBy_ContinuationMarksCleanedBeforeScrollingDown",
    "testScrollRectDownBy_Two_CleansContinuationMarksInFullWidth",
    "testScrollRectDownBy_NegativeOne_CleansContinuationMarksInFullWidth",
    "testScrollRectDownBy_NegativeTwo_CleansContinuationMarksInFullWidth",
    "testScrollRectDownBy_One_CleansContinuationMarksWithPartialWidth",
)


ROWS = put_rows(b"abcd", b"efgh", b"ijkl", b"mnop")
LARGE_ROWS = put_rows(b"abcde", b"fghij", b"klmno", b"pqrst", b"uvwxy")


class ITerm2VT100GridTest(unittest.TestCase):
    def test_upstream_inventory_has_first_60_distinct_cases(self):
        self.assertEqual(len(PORTED_CASES), 60)
        self.assertEqual(len(set(PORTED_CASES)), 60)

    def test_append_line_to_line_buffer_is_visible_in_scrollback(self):
        with Shitty(columns=4, rows=4, save_lines=4) as terminal:
            terminal.write(ROWS + b"\x1b[4;1H\x1bD")
            self.assertEqual(terminal.scrollback_state()[0], 1)
            terminal.wheel_up()
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines[:2], ["abcd", "efgh"])
            self.assertFalse(snapshot.cell(3, 0).wrapped)

    def test_appended_lines_keep_hard_and_soft_continuation_marks(self):
        with Shitty(columns=4, rows=4, save_lines=4) as terminal:
            terminal.write(b"abcd\r\nefghi\x1b[4;1H\x1bD\x1bD")
            terminal.wheel_up(2)
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines[:2], ["abcd", "efgh"])
            self.assertFalse(snapshot.cell(3, 0).wrapped)
            self.assertTrue(snapshot.cell(3, 1).wrapped)

    def test_soft_continuation_then_hard_eol_is_one_logical_line(self):
        with Shitty(columns=4, rows=4, save_lines=4) as terminal:
            terminal.write(b"abcdefgh\r\n\x1b[4;1H\x1bD\x1bD")
            terminal.wheel_up(2)
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines[:2], ["abcd", "efgh"])
            self.assertTrue(snapshot.cell(3, 0).wrapped)
            self.assertFalse(snapshot.cell(3, 1).wrapped)

    def test_scrollback_roundtrip_preserves_cursor_column(self):
        with Shitty(columns=4, rows=4, save_lines=4) as terminal:
            terminal.write(ROWS + b"\x1b[4;3H")
            terminal.resize(4, 2)
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, ["ijkl", "mnop"])
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (2, 1))
            self.assertEqual(terminal.scrollback_state()[0], 2)
            terminal.resize(4, 4)
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, ["abcd", "efgh", "ijkl", "mnop"])
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (2, 3))

    def test_blank_cursor_after_soft_eol_reflows_to_logical_line_end(self):
        with Shitty(columns=4, rows=4, save_lines=4) as terminal:
            terminal.write(b"abcdefghi\x1b[3;1H\x1b[P")
            terminal.resize(8, 4)
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines[0], "abcdefgh")
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (7, 0))
            self.assertTrue(terminal.cursor_pending_wrap())
            terminal.write(b"X")
            self.assertEqual(terminal.snapshot().lines[:2], ["abcdefgh", "X       "])

    def test_public_line_lengths_match_drawn_cell_prefixes(self):
        with Shitty(columns=4, rows=4) as terminal:
            terminal.write(put_rows(b"abcd", b"efg"))
            snapshot = terminal.snapshot()
            self.assertEqual(
                [
                    sum(snapshot.cell(column, row).drawn for column in range(4))
                    for row in range(3)
                ],
                [4, 3, 0],
            )

    def test_index_without_boundary_moves_down_without_scrolling(self):
        with Shitty(columns=4, rows=4, save_lines=4) as terminal:
            terminal.write(ROWS + b"\x1b[1;1H\x1bD")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, ["abcd", "efgh", "ijkl", "mnop"])
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 1))
            self.assertEqual(terminal.scrollback_state()[0], 0)

    def test_index_below_scroll_region_moves_until_page_bottom(self):
        with Shitty(columns=4, rows=4, save_lines=4) as terminal:
            terminal.write(ROWS + b"\x1b[1;2r\x1b[3;1H\x1bD")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, ["abcd", "efgh", "ijkl", "mnop"])
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 3))
            self.assertEqual(terminal.scrollback_state()[0], 0)

    def test_index_at_full_screen_bottom_scrolls_into_history(self):
        with Shitty(columns=4, rows=4, save_lines=4) as terminal:
            terminal.write(ROWS + b"\x1b[4;1H\x1bD")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, ["efgh", "ijkl", "mnop", "    "])
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 3))
            self.assertEqual(terminal.scrollback_state()[0], 1)
            terminal.wheel_up()
            self.assertEqual(terminal.snapshot().lines[0], "abcd")

    def test_full_screen_scroll_preserves_soft_eol_in_history(self):
        with Shitty(columns=4, rows=4, save_lines=4) as terminal:
            terminal.write(
                b"abcde"
                b"\x1b[2;1Hefgh"
                b"\x1b[3;1Hijkl"
                b"\x1b[4;1Hmnop"
                b"\x1b[4;1H\x1bD"
            )
            terminal.wheel_up()
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines[:2], ["abcd", "efgh"])
            self.assertTrue(snapshot.cell(3, 0).wrapped)

    def test_top_anchored_full_width_region_scrolls_only_its_rows(self):
        with Shitty(columns=4, rows=4, save_lines=4) as terminal:
            terminal.write(ROWS + b"\x1b[1;2r\x1b[2;1H\x1bD")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, ["efgh", "    ", "ijkl", "mnop"])
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 1))
            self.assertEqual(terminal.scrollback_state()[0], 1)

    def test_non_top_region_scroll_does_not_create_scrollback(self):
        with Shitty(columns=4, rows=4, save_lines=4) as terminal:
            terminal.write(ROWS + b"\x1b[2;3r\x1b[3;1H\x1bD")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, ["abcd", "ijkl", "    ", "mnop"])
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 2))
            self.assertEqual(terminal.scrollback_state()[0], 0)

    def test_bounded_scrollback_keeps_only_the_newest_scrolled_line(self):
        with Shitty(columns=4, rows=4, save_lines=1) as terminal:
            terminal.write(ROWS + b"\x1b[4;1H\x1bD\x1bD\x1bD")
            self.assertEqual(terminal.snapshot().lines, ["mnop", "    ", "    ", "    "])
            self.assertEqual(terminal.scrollback_state()[0], 1)
            terminal.wheel_up()
            self.assertEqual(terminal.snapshot().lines[:2], ["ijkl", "mnop"])

    def test_row_and_column_region_scrolls_only_the_rectangle(self):
        with Shitty(columns=4, rows=4, save_lines=4) as terminal:
            terminal.write(
                ROWS
                + b"\x1b[?69h"
                + b"\x1b[2;3s"
                + b"\x1b[2;3r"
                + b"\x1b[3;2H\x1bD"
            )
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, ["abcd", "ejkh", "i  l", "mnop"])
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (1, 2))
            self.assertEqual(terminal.scrollback_state()[0], 0)

    def test_cursor_left_default_clamps_at_page_edge(self):
        with Shitty(columns=4, rows=4) as terminal:
            terminal.write(b"\x1b[1;2H\x1b[D\x1b[D")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 0))

    def test_cursor_left_at_horizontal_margin_does_not_move(self):
        with Shitty(columns=4, rows=4) as terminal:
            terminal.write(b"\x1b[?69h\x1b[2;3s\x1b[1;2H\x1b[D")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (1, 0))

    def test_cursor_left_within_horizontal_margin_moves_to_margin(self):
        with Shitty(columns=4, rows=4) as terminal:
            terminal.write(b"\x1b[?69h\x1b[2;3s\x1b[1;3H\x1b[D")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (1, 0))

    def test_cursor_left_right_of_horizontal_margin_moves_once(self):
        with Shitty(columns=4, rows=4) as terminal:
            terminal.write(b"\x1b[?69h\x1b[2;3s\x1b[1;4H\x1b[D")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (2, 0))

    @unittest.expectedFailure
    def test_iterm_default_cub_wraps_across_soft_eol(self):
        with Shitty(columns=4, rows=4) as terminal:
            terminal.write(b"abcdef\x1b[4D")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (2, 0))

    @unittest.expectedFailure
    def test_iterm_default_cub_wraps_across_early_wide_eol(self):
        with Shitty(columns=3, rows=3) as terminal:
            terminal.write("ab界".encode("utf-8") + b"\x1b[4D")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (1, 0))
            self.assertTrue(snapshot.cell(0, 1).double_width)
            self.assertTrue(snapshot.cell(1, 1).double_width_continuation)

    def test_cursor_left_does_not_cross_a_hard_eol(self):
        with Shitty(columns=4, rows=4) as terminal:
            terminal.write(b"abc\r\nd\x1b[4D")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines[:2], ["abc ", "d   "])
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 1))

    def test_cursor_right_default_moves_one_column(self):
        with Shitty(columns=4, rows=4) as terminal:
            terminal.write(b"\x1b[1;3H\x1b[C")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (3, 0))

    def test_cursor_right_before_horizontal_region_moves_normally(self):
        with Shitty(columns=5, rows=4) as terminal:
            terminal.write(b"\x1b[?69h\x1b[3;4s\x1b[1;1H\x1b[C")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (1, 0))

    def test_cursor_right_can_enter_horizontal_region(self):
        with Shitty(columns=5, rows=4) as terminal:
            terminal.write(b"\x1b[?69h\x1b[3;4s\x1b[1;2H\x1b[C")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (2, 0))

    def test_cursor_right_stops_at_horizontal_region_end(self):
        with Shitty(columns=5, rows=4) as terminal:
            terminal.write(b"\x1b[?69h\x1b[3;4s\x1b[1;4H\x1b[C")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (3, 0))

    def test_cursor_up_default_clamps_at_page_top(self):
        with Shitty(columns=4, rows=4) as terminal:
            terminal.write(b"\x1b[3;1H\x1b[A")
            self.assertEqual(terminal.snapshot().cursor_y, 1)
            terminal.write(b"\x1b[A")
            self.assertEqual(terminal.snapshot().cursor_y, 0)
            terminal.write(b"\x1b[A")
            self.assertEqual(terminal.snapshot().cursor_y, 0)

    def test_cursor_up_clamps_at_vertical_region_top(self):
        with Shitty(columns=4, rows=4) as terminal:
            terminal.write(b"\x1b[2;3r\x1b[3;1H\x1b[A")
            self.assertEqual(terminal.snapshot().cursor_y, 1)
            terminal.write(b"\x1b[A")
            self.assertEqual(terminal.snapshot().cursor_y, 1)

    def test_cursor_up_above_vertical_region_moves_to_page_top(self):
        with Shitty(columns=4, rows=4) as terminal:
            terminal.write(b"\x1b[3;4r\x1b[2;1H\x1b[A")
            self.assertEqual(terminal.snapshot().cursor_y, 0)

    def test_cursor_down_default_clamps_at_page_bottom(self):
        with Shitty(columns=4, rows=4) as terminal:
            terminal.write(b"\x1b[3;1H\x1b[B")
            self.assertEqual(terminal.snapshot().cursor_y, 3)
            terminal.write(b"\x1b[B")
            self.assertEqual(terminal.snapshot().cursor_y, 3)

    def test_cursor_down_clamps_at_vertical_region_bottom(self):
        with Shitty(columns=4, rows=4) as terminal:
            terminal.write(b"\x1b[2;3r\x1b[2;1H\x1b[B")
            self.assertEqual(terminal.snapshot().cursor_y, 2)
            terminal.write(b"\x1b[B")
            self.assertEqual(terminal.snapshot().cursor_y, 2)

    def test_cursor_down_below_vertical_region_moves_to_page_bottom(self):
        with Shitty(columns=4, rows=4) as terminal:
            terminal.write(b"\x1b[1;2r\x1b[3;1H\x1b[B")
            self.assertEqual(terminal.snapshot().cursor_y, 3)
            terminal.write(b"\x1b[B")
            self.assertEqual(terminal.snapshot().cursor_y, 3)

    def test_scroll_up_moves_top_row_into_history(self):
        with Shitty(columns=4, rows=4, save_lines=4) as terminal:
            terminal.write(ROWS + b"\x1b[S")
            self.assertEqual(terminal.snapshot().lines, ["efgh", "ijkl", "mnop", "    "])
            self.assertEqual(terminal.scrollback_state()[0], 1)
            terminal.wheel_up()
            self.assertEqual(terminal.snapshot().lines[0], "abcd")

    def test_scroll_up_bounded_history_drops_oldest_row(self):
        with Shitty(columns=4, rows=4, save_lines=1) as terminal:
            terminal.write(ROWS)
            terminal.write(b"\x1b[S")
            self.assertEqual(terminal.scrollback_state()[0], 1)
            terminal.write(b"\x1b[S")
            self.assertEqual(terminal.scrollback_state()[0], 1)
            self.assertEqual(terminal.snapshot().lines, ["ijkl", "mnop", "    ", "    "])
            terminal.wheel_up()
            self.assertEqual(terminal.snapshot().lines[:2], ["efgh", "ijkl"])

    def test_horizontal_region_scroll_does_not_create_history(self):
        with Shitty(columns=4, rows=4, save_lines=4) as terminal:
            terminal.write(ROWS + b"\x1b[?69h\x1b[2;3s\x1b[S")
            self.assertEqual(
                terminal.snapshot().lines,
                ["afgd", "ejkh", "inol", "m  p"],
            )
            self.assertEqual(terminal.scrollback_state()[0], 0)

    def test_repeated_whole_screen_scroll_keeps_newest_history_tail(self):
        with Shitty(columns=4, rows=4, save_lines=1) as terminal:
            terminal.write(ROWS + b"\x1b[S\x1b[4;1Hqrst\x1b[S")
            self.assertEqual(terminal.snapshot().lines, ["ijkl", "mnop", "qrst", "    "])
            self.assertEqual(terminal.scrollback_state()[0], 1)
            terminal.wheel_up()
            self.assertEqual(terminal.snapshot().lines[:2], ["efgh", "ijkl"])

    def test_wire_zero_scroll_down_defaults_to_one_row(self):
        with Shitty(columns=4, rows=4) as terminal:
            terminal.write(ROWS + b"\x1b[?69h\x1b[2;3s\x1b[2;3r")
            terminal.write(b"\x1b[0T")
            self.assertEqual(
                terminal.snapshot().lines,
                ["abcd", "e  h", "ifgl", "mnop"],
            )
            self.assertEqual(terminal.last_update_rows(), (1, 2))

    def test_rectangular_scroll_down_one_damages_region_rows(self):
        with Shitty(columns=4, rows=4) as terminal:
            terminal.write(ROWS + b"\x1b[?69h\x1b[2;3s\x1b[2;3r")
            terminal.write(b"\x1b[T")
            self.assertEqual(
                terminal.snapshot().lines,
                ["abcd", "e  h", "ifgl", "mnop"],
            )
            self.assertEqual(terminal.last_update_rows(), (1, 2))

    def test_rectangular_scroll_up_one_damages_region_rows(self):
        with Shitty(columns=4, rows=4) as terminal:
            terminal.write(ROWS + b"\x1b[?69h\x1b[2;3s\x1b[2;3r")
            terminal.write(b"\x1b[S")
            self.assertEqual(
                terminal.snapshot().lines,
                ["abcd", "ejkh", "i  l", "mnop"],
            )
            self.assertEqual(terminal.last_update_rows(), (1, 2))

    def test_rectangular_scroll_down_two_damages_region_rows(self):
        with Shitty(columns=5, rows=5) as terminal:
            terminal.write(LARGE_ROWS + b"\x1b[?69h\x1b[2;4s\x1b[2;4r")
            terminal.write(b"\x1b[2T")
            self.assertEqual(
                terminal.snapshot().lines,
                ["abcde", "f   j", "k   o", "pghit", "uvwxy"],
            )
            self.assertEqual(terminal.last_update_rows(), (1, 2, 3))

    def test_rectangular_scroll_up_two_damages_region_rows(self):
        with Shitty(columns=5, rows=5) as terminal:
            terminal.write(LARGE_ROWS + b"\x1b[?69h\x1b[2;4s\x1b[2;4r")
            terminal.write(b"\x1b[2S")
            self.assertEqual(
                terminal.snapshot().lines,
                ["abcde", "fqrsj", "k   o", "p   t", "uvwxy"],
            )
            self.assertEqual(terminal.last_update_rows(), (1, 2, 3))

    def test_rectangular_scroll_down_by_region_height_blanks_region(self):
        with Shitty(columns=5, rows=5) as terminal:
            terminal.write(LARGE_ROWS + b"\x1b[?69h\x1b[2;4s\x1b[2;4r")
            terminal.write(b"\x1b[3T")
            self.assertEqual(
                terminal.snapshot().lines,
                ["abcde", "f   j", "k   o", "p   t", "uvwxy"],
            )
            self.assertEqual(terminal.last_update_rows(), (1, 2, 3))

    def test_rectangular_scroll_up_by_region_height_blanks_region(self):
        with Shitty(columns=5, rows=5) as terminal:
            terminal.write(LARGE_ROWS + b"\x1b[?69h\x1b[2;4s\x1b[2;4r")
            terminal.write(b"\x1b[3S")
            self.assertEqual(
                terminal.snapshot().lines,
                ["abcde", "f   j", "k   o", "p   t", "uvwxy"],
            )
            self.assertEqual(terminal.last_update_rows(), (1, 2, 3))

    def test_rectangular_scroll_down_clamps_count_to_region_height(self):
        with Shitty(columns=5, rows=5) as terminal:
            terminal.write(LARGE_ROWS + b"\x1b[?69h\x1b[2;4s\x1b[2;4r")
            terminal.write(b"\x1b[4T")
            self.assertEqual(
                terminal.snapshot().lines,
                ["abcde", "f   j", "k   o", "p   t", "uvwxy"],
            )
            self.assertEqual(terminal.last_update_rows(), (1, 2, 3))

    def test_rectangular_scroll_up_clamps_count_to_region_height(self):
        with Shitty(columns=5, rows=5) as terminal:
            terminal.write(LARGE_ROWS + b"\x1b[?69h\x1b[2;4s\x1b[2;4r")
            terminal.write(b"\x1b[4S")
            self.assertEqual(
                terminal.snapshot().lines,
                ["abcde", "f   j", "k   o", "p   t", "uvwxy"],
            )
            self.assertEqual(terminal.last_update_rows(), (1, 2, 3))

    def test_partial_scroll_up_clears_a_wide_source_cut_at_left_edge(self):
        wide = "界".encode("utf-8")
        with Shitty(
            columns=7,
            rows=2,
            save_lines=0,
            extra_arguments=("-unicodeWidths", "17"),
        ) as terminal:
            terminal.write(
                put_rows(b"1234567", b"a" + wide + b"xyz")
                + b"\x1b[?69h\x1b[3;7s"
            )
            terminal.write(b"\x1b[S")
            self.assertEqual(terminal.snapshot().lines, ["12 xyz ", "a      "])
            self.assertEqual(terminal.last_update_rows(), (0, 1))

    def test_partial_scroll_down_clears_a_wide_source_cut_at_right_edge(self):
        wide = "界".encode("utf-8")
        with Shitty(
            columns=7,
            rows=2,
            save_lines=0,
            extra_arguments=("-unicodeWidths", "17"),
        ) as terminal:
            terminal.write(
                put_rows(b"abc" + wide + b"yz", b"1234567")
                + b"\x1b[?69h\x1b[1;4s"
            )
            terminal.write(b"\x1b[T")
            self.assertEqual(terminal.snapshot().lines, ["     yz", "abc 567"])
            self.assertEqual(terminal.last_update_rows(), (0, 1))

    def test_full_width_scroll_moves_a_wrapped_wide_glyph_intact(self):
        wide = "界".encode("utf-8")
        with Shitty(
            columns=3,
            rows=3,
            save_lines=0,
            extra_arguments=("-unicodeWidths", "17"),
        ) as terminal:
            terminal.write(b"ab" + wide + b"cd")
            terminal.write(b"\x1b[S")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, ["界 c", "d  ", "   "])
            self.assertTrue(snapshot.cell(0, 0).double_width)
            self.assertTrue(snapshot.cell(1, 0).double_width_continuation)
            self.assertEqual(terminal.last_update_rows(), (0, 1, 2))

    def test_partial_scroll_cleans_orphaned_wide_cells_on_every_row(self):
        wide = "界".encode("utf-8")
        with Shitty(
            columns=7,
            rows=6,
            save_lines=0,
            extra_arguments=("-unicodeWidths", "17"),
        ) as terminal:
            terminal.write(
                put_rows(
                    b"a" + wide + b"A" + wide + b"z",
                    b"b" + wide + b"B" + wide + b"y",
                    b"c" + wide + b"C" + wide + b"x",
                    b"d" + wide + b"D" + wide + b"w",
                    b"e" + wide + b"E" + wide + b"v",
                    b"f" + wide + b"F" + wide + b"u",
                )
                + b"\x1b[?69h\x1b[3;5s"
            )
            terminal.write(b"\x1b[S")
            snapshot = terminal.snapshot()
            self.assertEqual(
                snapshot.lines,
                ["a  B  z", "b  C  y", "c  D  x", "d  E  w", "e  F  v", "f     u"],
            )
            self.assertFalse(any(cell.double_width for cell in snapshot.cells))
            self.assertFalse(any(cell.double_width_continuation for cell in snapshot.cells))
            self.assertEqual(terminal.last_update_rows(), tuple(range(6)))

    def test_partial_scroll_repairs_destination_edges_only_inside_vertical_region(self):
        wide = "界".encode("utf-8")
        with Shitty(
            columns=7,
            rows=3,
            save_lines=0,
            extra_arguments=("-unicodeWidths", "17"),
        ) as terminal:
            terminal.write(
                put_rows(
                    b"a" + wide + b"x" + wide + b"z",
                    b"b" + wide + b"y" + wide + b"q",
                    b"c12v34r",
                )
                + b"\x1b[?69h\x1b[3;5s\x1b[2;3r"
            )
            terminal.write(b"\x1b[S")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, ["a界 x界 z", "b 2v3 q", "c1   4r"])
            self.assertTrue(snapshot.cell(1, 0).double_width)
            self.assertTrue(snapshot.cell(2, 0).double_width_continuation)
            self.assertEqual(terminal.last_update_rows(), (1, 2))

    def test_partial_scroll_to_right_margin_repairs_left_wide_boundary(self):
        wide = "界".encode("utf-8")
        with Shitty(
            columns=7,
            rows=2,
            save_lines=0,
            extra_arguments=("-unicodeWidths", "17"),
        ) as terminal:
            terminal.write(
                put_rows(b"1234567", wide + b"ABCDE")
                + b"\x1b[?69h\x1b[2;7s"
            )
            terminal.write(b"\x1b[S")
            self.assertEqual(terminal.snapshot().lines, ["1 ABCDE", "       "])
            self.assertEqual(terminal.last_update_rows(), (0, 1))

    def test_partial_scroll_from_left_margin_repairs_right_wide_boundary(self):
        wide = "界".encode("utf-8")
        with Shitty(
            columns=7,
            rows=2,
            save_lines=0,
            extra_arguments=("-unicodeWidths", "17"),
        ) as terminal:
            terminal.write(
                put_rows(b"1234567", b"ABCDE" + wide)
                + b"\x1b[?69h\x1b[1;6s"
            )
            terminal.write(b"\x1b[S")
            self.assertEqual(terminal.snapshot().lines, ["ABCDE 7", "       "])
            self.assertEqual(terminal.last_update_rows(), (0, 1))

    def test_invalid_zero_height_scroll_region_is_harmless(self):
        with Shitty(columns=4, rows=4) as terminal:
            terminal.write(ROWS)
            terminal.write(b"\x1b[2;2r")
            self.assertEqual(terminal.snapshot().lines, ["abcd", "efgh", "ijkl", "mnop"])
            self.assertEqual(terminal.last_update_rows(), ())

    def test_partial_scroll_preserves_complete_wide_and_repairs_cut_one(self):
        wide = "界".encode("utf-8")
        with Shitty(
            columns=7,
            rows=3,
            save_lines=0,
            extra_arguments=("-unicodeWidths", "17"),
        ) as terminal:
            terminal.write(
                put_rows(b"1234567", wide + b"A" + wide + b"B")
                + b"\x1b[?69h\x1b[2;6s\x1b[1;2r"
            )
            terminal.write(b"\x1b[S")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines[0], "1 A界 B7")
            self.assertTrue(snapshot.cell(3, 0).double_width)
            self.assertTrue(snapshot.cell(4, 0).double_width_continuation)
            self.assertEqual(terminal.last_update_rows(), (0, 1))

    def test_partial_scroll_down_keeps_complete_wide_at_rectangle_edge(self):
        wide = "界".encode("utf-8")
        with Shitty(
            columns=7,
            rows=2,
            save_lines=0,
            extra_arguments=("-unicodeWidths", "17"),
        ) as terminal:
            terminal.write(
                put_rows(b"aBC" + wide + b"z", b"1234567")
                + b"\x1b[?69h\x1b[2;5s"
            )
            terminal.write(b"\x1b[T")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, ["a    z ", "1BC界 67"])
            self.assertTrue(snapshot.cell(3, 1).double_width)
            self.assertTrue(snapshot.cell(4, 1).double_width_continuation)
            self.assertEqual(terminal.last_update_rows(), (0, 1))

    def test_partial_scroll_up_keeps_complete_wide_at_rectangle_edge(self):
        wide = "界".encode("utf-8")
        with Shitty(
            columns=7,
            rows=2,
            save_lines=0,
            extra_arguments=("-unicodeWidths", "17"),
        ) as terminal:
            terminal.write(
                put_rows(b"1234567", b"aBC" + wide + b"z")
                + b"\x1b[?69h\x1b[2;5s"
            )
            terminal.write(b"\x1b[S")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, ["1BC界 67", "a    z "])
            self.assertTrue(snapshot.cell(3, 0).double_width)
            self.assertTrue(snapshot.cell(4, 0).double_width_continuation)
            self.assertEqual(terminal.last_update_rows(), (0, 1))

    def test_full_width_scroll_down_preserves_moved_soft_wrap_metadata(self):
        with Shitty(columns=4, rows=5, save_lines=0) as terminal:
            terminal.write(b"abcdefghijklmnopqrst\x1b[2;4r")
            terminal.write(b"\x1b[T")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, ["abcd", "    ", "efgh", "ijkl", "qrst"])
            self.assertEqual([snapshot.cell(3, row).wrapped for row in range(5)], [True, False, True, True, False])
            self.assertEqual(terminal.last_update_rows(), (1, 2, 3))

    def test_full_width_scroll_down_two_preserves_source_soft_wrap(self):
        with Shitty(columns=4, rows=5, save_lines=0) as terminal:
            terminal.write(b"abcdefghijklmnopqrst\x1b[2;4r")
            terminal.write(b"\x1b[2T")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, ["abcd", "    ", "    ", "efgh", "qrst"])
            self.assertEqual([snapshot.cell(3, row).wrapped for row in range(5)], [True, False, False, True, False])
            self.assertEqual(terminal.last_update_rows(), (1, 2, 3))

    def test_full_width_scroll_up_preserves_moved_soft_wrap_metadata(self):
        with Shitty(columns=4, rows=5, save_lines=0) as terminal:
            terminal.write(b"abcdefghijklmnopqrst\x1b[2;4r")
            terminal.write(b"\x1b[S")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, ["abcd", "ijkl", "mnop", "    ", "qrst"])
            self.assertEqual([snapshot.cell(3, row).wrapped for row in range(5)], [True, True, True, False, False])
            self.assertEqual(terminal.last_update_rows(), (1, 2, 3))

    def test_full_width_scroll_up_two_preserves_source_soft_wrap(self):
        with Shitty(columns=4, rows=5, save_lines=0) as terminal:
            terminal.write(b"abcdefghijklmnopqrst\x1b[2;4r")
            terminal.write(b"\x1b[2S")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, ["abcd", "mnop", "    ", "    ", "qrst"])
            self.assertEqual([snapshot.cell(3, row).wrapped for row in range(5)], [True, True, False, False, False])
            self.assertEqual(terminal.last_update_rows(), (1, 2, 3))

    def test_partial_width_scroll_preserves_each_rows_soft_wrap_metadata(self):
        with Shitty(columns=4, rows=5, save_lines=0) as terminal:
            terminal.write(
                b"abcdefghijklmnopqrst"
                + b"\x1b[?69h\x1b[2;4s\x1b[2;4r"
            )
            terminal.write(b"\x1b[T")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, ["abcd", "e   ", "ifgh", "mjkl", "qrst"])
            self.assertEqual([snapshot.cell(3, row).wrapped for row in range(5)], [True, True, True, True, False])
            self.assertEqual(terminal.last_update_rows(), (1, 2, 3))


if __name__ == "__main__":
    unittest.main()
