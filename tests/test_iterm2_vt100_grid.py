# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

"""Public adaptations of the first 40 iTerm2 VT100Grid cases."""

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
)


ROWS = put_rows(b"abcd", b"efgh", b"ijkl", b"mnop")
LARGE_ROWS = put_rows(b"abcde", b"fghij", b"klmno", b"pqrst", b"uvwxy")


class ITerm2VT100GridTest(unittest.TestCase):
    def test_upstream_inventory_has_first_40_distinct_cases(self):
        self.assertEqual(len(PORTED_CASES), 40)
        self.assertEqual(len(set(PORTED_CASES)), 40)

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


if __name__ == "__main__":
    unittest.main()
