# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

"""Public adaptations of xterm.js Buffer cases 1 through 20."""

import unittest

from harness import Shitty


UPSTREAM_CASES = (
    "line storage capacity equals rows plus scrollback",
    "the initial scrolling-region bottom equals rows minus one",
    "filling the viewport creates one blank line per visible row",
    "a non-wrapped first row is one logical line",
    "a non-wrapped middle row is one logical line",
    "a non-wrapped last row is one logical line",
    "the first row includes its wrapped continuation",
    "a middle continuation includes the row above",
    "a middle row includes the continuation below",
    "a middle continuation includes the complete wrapped block",
    "the last row includes the wrapped row above",
    "a continuation can wrap upward to the first row",
    "a row can wrap downward to the last row",
    "width reduction resizes every blank viewport row",
    "width growth pads every blank viewport row",
    "height reduction trims blank rows from the end",
    "height reduction exposes backing rows when the cursor is above the tail",
    "height reduction without scrollback trims rows above the bottom cursor",
    "height growth appends blank rows to an empty buffer",
    "height growth reveals more backing rows above the viewport",
)


def select_line(terminal, column, row):
    terminal.select_start(column, row)
    terminal.select_extend(column, row, cycle=True)
    terminal.select_extend(column, row, cycle=True)
    return terminal.select_finish()


def put_at(row, text):
    return f"\x1b[{row + 1};1H".encode() + text


class XtermJsBufferTest(unittest.TestCase):
    def test_upstream_inventory_has_20_distinct_cases(self):
        self.assertEqual(len(UPSTREAM_CASES), 20)
        self.assertEqual(len(set(UPSTREAM_CASES)), 20)

    def test_line_storage_capacity_equals_rows_plus_scrollback(self):
        with Shitty(columns=4, rows=3, save_lines=2) as terminal:
            terminal.write(b"0\r\n1\r\n2\r\n3\r\n4\r\n5")
            self.assertEqual(terminal.scrollback_state(), (2, 5, 3, 2))
            terminal.wheel_up(100)
            self.assertEqual(
                tuple(line.rstrip() for line in terminal.snapshot().lines),
                ("1", "2", "3"),
            )

    def test_initial_scrolling_region_bottom_is_the_last_row(self):
        with Shitty(columns=4, rows=4, save_lines=2) as terminal:
            terminal.write(b"\x1b[4;1HA\r\nB")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines[-2:], ["A   ", "B   "])
            self.assertEqual(terminal.scrollback_state()[0], 1)

    def test_fill_viewport_creates_blank_rows(self):
        with Shitty(columns=80, rows=24, save_lines=1000) as terminal:
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.columns, snapshot.rows), (80, 24))
            self.assertEqual(snapshot.lines, [" " * 80] * 24)
            self.assertEqual(terminal.scrollback_state(), (0, 24, 24, 0))

    def test_nonwrapped_first_row_is_one_logical_line(self):
        with Shitty(columns=8, rows=24) as terminal:
            terminal.write(put_at(0, b"first"))
            self.assertEqual(select_line(terminal, 1, 0), b"first")

    def test_nonwrapped_middle_row_is_one_logical_line(self):
        with Shitty(columns=8, rows=24) as terminal:
            terminal.write(put_at(12, b"middle"))
            self.assertEqual(select_line(terminal, 1, 12), b"middle")

    def test_nonwrapped_last_row_is_one_logical_line(self):
        with Shitty(columns=8, rows=24) as terminal:
            terminal.write(put_at(23, b"last"))
            self.assertEqual(select_line(terminal, 1, 23), b"last")

    def test_first_row_includes_its_wrapped_continuation(self):
        with Shitty(columns=4, rows=24) as terminal:
            terminal.write(put_at(0, b"ABCDEFGH"))
            self.assertEqual(select_line(terminal, 1, 0), b"ABCDEFGH")

    def test_middle_continuation_includes_the_row_above(self):
        with Shitty(columns=4, rows=24) as terminal:
            terminal.write(put_at(11, b"ABCDEFGH"))
            self.assertEqual(select_line(terminal, 1, 12), b"ABCDEFGH")

    def test_middle_row_includes_the_continuation_below(self):
        with Shitty(columns=4, rows=24) as terminal:
            terminal.write(put_at(12, b"ABCDEFGH"))
            self.assertEqual(select_line(terminal, 1, 12), b"ABCDEFGH")

    def test_middle_continuation_includes_the_complete_wrapped_block(self):
        with Shitty(columns=4, rows=24) as terminal:
            terminal.write(put_at(10, b"ABCDEFGHIJKLMNOPQRST"))
            self.assertEqual(
                select_line(terminal, 1, 12),
                b"ABCDEFGHIJKLMNOPQRST",
            )

    def test_last_row_includes_the_wrapped_row_above(self):
        with Shitty(columns=4, rows=24) as terminal:
            terminal.write(put_at(22, b"ABCDEFGH"))
            self.assertEqual(select_line(terminal, 1, 23), b"ABCDEFGH")

    def test_continuation_can_wrap_upward_to_the_first_row(self):
        with Shitty(columns=4, rows=24) as terminal:
            terminal.write(put_at(0, b"ABCDEFGH"))
            self.assertEqual(select_line(terminal, 1, 1), b"ABCDEFGH")

    def test_row_can_wrap_downward_to_the_last_row(self):
        with Shitty(columns=4, rows=24) as terminal:
            terminal.write(put_at(22, b"ABCDEFGH"))
            self.assertEqual(select_line(terminal, 1, 22), b"ABCDEFGH")

    def test_width_reduction_resizes_every_blank_viewport_row(self):
        with Shitty(columns=80, rows=24) as terminal:
            terminal.resize(40, 24)
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.columns, snapshot.rows), (40, 24))
            self.assertEqual(snapshot.lines, [" " * 40] * 24)

    def test_width_growth_pads_every_blank_viewport_row(self):
        with Shitty(columns=80, rows=24) as terminal:
            terminal.resize(90, 24)
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.columns, snapshot.rows), (90, 24))
            self.assertEqual(snapshot.lines, [" " * 90] * 24)

    def test_height_reduction_trims_blank_rows_from_the_end(self):
        with Shitty(columns=80, rows=24) as terminal:
            terminal.resize(80, 14)
            self.assertEqual(terminal.scrollback_state(), (0, 14, 14, 0))
            self.assertEqual(terminal.snapshot().lines, [" " * 80] * 14)

    def test_height_reduction_exposes_backing_rows_above_the_cursor(self):
        with Shitty(columns=80, rows=24, save_lines=1000) as terminal:
            terminal.write(b"\x1b[19;1H")
            terminal.resize(80, 14)
            snapshot = terminal.snapshot()
            self.assertEqual(terminal.scrollback_state(), (5, 19, 14, 5))
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 13))

    def test_height_reduction_without_scrollback_trims_above_bottom_cursor(self):
        with Shitty(columns=80, rows=24, save_lines=0) as terminal:
            terminal.write(put_at(5, b"a") + put_at(23, b"b"))
            terminal.resize(80, 19)
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines[0][0], "a")
            self.assertEqual(snapshot.lines[18][0], "b")
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (1, 18))

    def test_height_growth_appends_blank_rows_to_an_empty_buffer(self):
        with Shitty(columns=80, rows=24) as terminal:
            terminal.resize(80, 34)
            self.assertEqual(terminal.scrollback_state(), (0, 34, 34, 0))
            self.assertEqual(terminal.snapshot().lines, [" " * 80] * 34)

    def test_height_growth_reveals_more_backing_rows_above_the_viewport(self):
        with Shitty(columns=80, rows=24, save_lines=1000) as terminal:
            terminal.write(b"\r\n" * 33)
            self.assertEqual(terminal.scrollback_state(), (10, 34, 24, 10))
            terminal.resize(80, 29)
            self.assertEqual(terminal.scrollback_state(), (5, 34, 29, 5))


if __name__ == "__main__":
    unittest.main()
