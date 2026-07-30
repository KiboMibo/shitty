# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import unittest

from harness import Shitty


def osc66(metadata, text):
    return b"\x1b]66;" + metadata + b";" + text + b"\x07"


class KittyTextSizingTest(unittest.TestCase):
    def test_width_and_scale_advance_the_cursor(self):
        with Shitty(columns=20, rows=4) as terminal:
            terminal.write(osc66(b"w=2", b" "))
            self.assertEqual(terminal.snapshot().cursor_x, 2)
            self.assertEqual(terminal.multicell(0, 0).columns, 2)

            terminal.write(osc66(b"s=2:w=3", b"X"))
            self.assertEqual(terminal.snapshot().cursor_x, 8)
            block = terminal.multicell(0, 2)
            self.assertEqual((block.columns, block.rows, block.scale), (6, 2, 2))

    def test_without_width_each_grapheme_is_a_separate_block(self):
        with Shitty(columns=20, rows=4) as terminal:
            terminal.write(osc66(b"s=2", b"ab"))
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.cursor_x, 4)
            self.assertEqual(snapshot.cell(0, 0).char, "a")
            self.assertEqual(snapshot.cell(2, 0).char, "b")
            self.assertTrue(terminal.multicell(0, 0).valid)
            self.assertTrue(terminal.multicell(0, 2).valid)
            self.assertEqual(terminal.multicell(0, 0).column, 0)
            self.assertEqual(terminal.multicell(0, 2).column, 0)

    def test_grapheme_cluster_is_not_split(self):
        with Shitty(columns=10, rows=3) as terminal:
            terminal.write(osc66(b"s=2", "a\u0301".encode()))
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.cursor_x, 2)
            self.assertEqual(snapshot.cell(0, 0).grapheme, (ord("a"), 0x301))
            self.assertEqual(snapshot.cell(1, 0).grapheme, ())

    def test_default_request_has_ordinary_geometry(self):
        with Shitty(columns=10, rows=3) as terminal:
            terminal.write(osc66(b"", b"abc"))
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cursor_x, 3)
            self.assertEqual(snapshot.lines[0][:3], "abc")
            self.assertEqual(terminal.multicell(0, 0).scale, 1)

    def test_block_moves_whole_to_the_next_line(self):
        with Shitty(columns=5, rows=3) as terminal:
            terminal.write(b"abcd" + osc66(b"w=3", b"X"))
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (3, 1))
            self.assertEqual(snapshot.cell(0, 1).char, "X")
            self.assertEqual(terminal.multicell(1, 0).columns, 3)

    def test_block_larger_than_the_page_is_dropped(self):
        with Shitty(columns=4, rows=2) as terminal:
            terminal.write(osc66(b"s=3:w=3", b"X"))
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 0))
            self.assertFalse(terminal.multicell(0, 0).valid)

        with Shitty(columns=10, rows=2) as terminal:
            terminal.write(osc66(b"s=4", b"X"))
            self.assertFalse(terminal.multicell(0, 0).valid)

    def test_without_autowrap_block_is_clamped_to_right_edge(self):
        with Shitty(columns=6, rows=2) as terminal:
            terminal.write(b"\x1b[?7labcde" + osc66(b"w=3", b"X"))
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (5, 0))
            self.assertEqual(snapshot.cell(3, 0).char, "X")
            self.assertEqual(terminal.multicell(0, 3).columns, 3)

    def test_block_scrolls_to_make_vertical_room(self):
        with Shitty(columns=10, rows=4, save_lines=10) as terminal:
            terminal.write(b"one\r\ntwo\r\nthree\r\nfour\r\n")
            terminal.write(osc66(b"s=2", b"X"))
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(0, 2).char, "X")
            self.assertEqual(terminal.multicell(2, 0).row, 0)
            self.assertEqual(terminal.multicell(3, 0).row, 1)

    def test_full_sizing_metadata_survives(self):
        with Shitty(columns=20, rows=6) as terminal:
            terminal.write(osc66(b"s=3:w=1:n=1:d=3:v=2:h=1", b"x"))
            block = terminal.multicell(0, 0)
            self.assertEqual(
                (
                    block.columns,
                    block.rows,
                    block.scale,
                    block.width,
                    block.numerator,
                    block.denominator,
                    block.vertical_alignment,
                    block.horizontal_alignment,
                ),
                (3, 3, 3, 1, 1, 3, 2, 1),
            )

    def test_ordinary_write_over_head_clears_the_whole_block(self):
        with Shitty(columns=10, rows=4) as terminal:
            terminal.write(osc66(b"s=2:w=2", b"X"))
            terminal.write(b"\x1b[Hhello")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines[0][:5], "hello")
            for row in range(2):
                for column in range(4):
                    self.assertFalse(terminal.multicell(row, column).valid)

    def test_ordinary_write_in_lower_band_skips_past_the_block(self):
        with Shitty(columns=10, rows=4) as terminal:
            terminal.write(osc66(b"s=2:w=2", b"X"))
            terminal.write(b"\x1b[2;2Hy")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(0, 0).char, "X")
            self.assertEqual(snapshot.cell(4, 1).char, "y")
            self.assertTrue(terminal.multicell(0, 0).valid)
            self.assertTrue(terminal.multicell(1, 3).valid)

    def test_erase_intersection_clears_the_whole_block(self):
        with Shitty(columns=10, rows=4) as terminal:
            terminal.write(osc66(b"s=3", b"X"))
            terminal.write(b"\x1b[2;1;3;1${")
            for row in range(3):
                for column in range(3):
                    self.assertFalse(terminal.multicell(row, column).valid)

    def test_insert_mode_shifts_each_claimed_row(self):
        with Shitty(columns=10, rows=3) as terminal:
            terminal.write(b"abcdef\x1b[2;1Hghijkl\x1b[H\x1b[4h")
            terminal.write(osc66(b"s=2", b"X"))
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(0, 0).char, "X")
            self.assertEqual(snapshot.cell(2, 0).char, "a")
            self.assertEqual(snapshot.cell(2, 1).char, "g")

    def test_ich_moves_an_intact_single_line_block(self):
        with Shitty(columns=12, rows=2) as terminal:
            terminal.write(b"\x1b[1;5H" + osc66(b"w=3", b"A"))
            terminal.write(b"\x1b[1;1H\x1b[2@")
            self.assertFalse(terminal.multicell(0, 4).valid)
            block = terminal.multicell(0, 6)
            self.assertTrue(block.valid)
            self.assertEqual((block.columns, block.column), (3, 0))

    def test_dch_moves_an_intact_single_line_block(self):
        with Shitty(columns=12, rows=2) as terminal:
            terminal.write(b"\x1b[1;5H" + osc66(b"w=3", b"A"))
            terminal.write(b"\x1b[1;1H\x1b[2P")
            block = terminal.multicell(0, 2)
            self.assertTrue(block.valid)
            self.assertEqual((block.columns, block.column), (3, 0))
            self.assertFalse(terminal.multicell(0, 6).valid)

    def test_ich_erases_a_single_line_block_split_by_its_boundary(self):
        with Shitty(columns=12, rows=2) as terminal:
            terminal.write(b"\x1b[1;5H" + osc66(b"w=3", b"A"))
            terminal.write(b"\x1b[1;6H\x1b[@")
            for column in range(12):
                self.assertFalse(terminal.multicell(0, column).valid)

    def test_dch_erases_a_single_line_block_split_by_its_source_boundary(self):
        with Shitty(columns=12, rows=2) as terminal:
            terminal.write(b"\x1b[1;5H" + osc66(b"w=3", b"A"))
            terminal.write(b"\x1b[1;4H\x1b[2P")
            for column in range(12):
                self.assertFalse(terminal.multicell(0, column).valid)

    def test_ich_erases_a_multiline_block_anywhere_in_the_shifted_tail(self):
        with Shitty(columns=12, rows=3) as terminal:
            terminal.write(b"\x1b[1;5H" + osc66(b"s=2", b"A"))
            terminal.write(b"\x1b[1;1H\x1b[2@")
            for row in range(2):
                for column in range(12):
                    self.assertFalse(terminal.multicell(row, column).valid)

    def test_selection_extracts_a_block_payload_once(self):
        with Shitty(columns=10, rows=2) as terminal:
            terminal.write(osc66(b"s=2:w=3", b"X") + b"z")
            terminal.select_start(0, 0)
            terminal.select_update(7, 0)
            self.assertEqual(terminal.select_finish(), b"Xz")

    def test_selection_from_a_top_continuation_extracts_the_block(self):
        with Shitty(columns=10, rows=2) as terminal:
            terminal.write(osc66(b"s=2:w=3", b"X"))
            terminal.select_start(5, 0)
            terminal.select_update(6, 0)
            self.assertEqual(terminal.select_finish(), b"X")

    def test_selection_from_a_lower_band_extracts_the_block(self):
        with Shitty(columns=10, rows=2) as terminal:
            terminal.write(osc66(b"s=2:w=3", b"X"))
            terminal.select_start(4, 1)
            terminal.select_update(5, 1)
            self.assertEqual(terminal.select_finish(), b"X")

    def test_selection_does_not_merge_adjacent_blocks(self):
        with Shitty(columns=10, rows=2) as terminal:
            terminal.write(osc66(b"w=2", b"A") + osc66(b"w=2", b"B"))
            terminal.select_start(0, 0)
            terminal.select_update(1, 0)
            self.assertEqual(terminal.select_finish(), b"A")

    def test_copying_lower_bands_adds_no_blank_trailing_line(self):
        with Shitty(columns=10, rows=2) as terminal:
            terminal.write(osc66(b"s=2:w=2", b"X"))
            terminal.select_start(0, 0)
            terminal.select_update(4, 1)
            self.assertEqual(terminal.select_finish(), b"X")

    def test_block_identity_and_band_survive_in_scrollback(self):
        with Shitty(columns=10, rows=4, save_lines=20) as terminal:
            terminal.write(osc66(b"s=3", b"X"))
            terminal.write(b"\x1b[4;1H\x1b[5S")
            history = terminal.scrollback_state()[0]
            blocks = [
                (row, terminal.multicell(row, 0))
                for row in range(-history, 0)
                if terminal.multicell(row, 0).valid
            ]
            self.assertEqual([row for row, _ in blocks], [-4, -3, -2])
            self.assertEqual([block.row for _, block in blocks], [0, 1, 2])
            self.assertTrue(all(block.rows == 3 for _, block in blocks))

    def test_deccra_copies_a_complete_block_with_every_band(self):
        with Shitty(columns=10, rows=8) as terminal:
            terminal.write(osc66(b"s=2", b"A"))
            terminal.write(b"\x1b[1;1;2;2;1;5;1;1$v")
            for row in range(2):
                for column in range(2):
                    block = terminal.multicell(4 + row, column)
                    self.assertTrue(block.valid)
                    self.assertEqual((block.row, block.column), (row, column))
                    self.assertEqual((block.rows, block.columns), (2, 2))

    def test_deccra_drops_a_partial_source_block(self):
        with Shitty(columns=10, rows=8) as terminal:
            terminal.write(osc66(b"s=2", b"A"))
            terminal.write(b"\x1b[1;1;1;2;1;5;1;1$v")
            for column in range(2):
                self.assertFalse(terminal.multicell(4, column).valid)
            self.assertTrue(terminal.multicell(0, 0).valid)
            self.assertTrue(terminal.multicell(1, 0).valid)

    def test_deccra_intersection_erases_the_old_destination_block(self):
        with Shitty(columns=10, rows=8) as terminal:
            terminal.write(b"A\x1b[5;1H" + osc66(b"s=2", b"X"))
            terminal.write(b"\x1b[1;1;1;1;1;5;1;1$v")
            for row in range(4, 6):
                for column in range(2):
                    self.assertFalse(terminal.multicell(row, column).valid)
            self.assertEqual(terminal.snapshot().cell(0, 4).char, "A")

    def test_adjacent_blocks_keep_distinct_identity(self):
        with Shitty(columns=12, rows=2) as terminal:
            terminal.write(osc66(b"w=2", b"A") + osc66(b"w=2", b"B"))
            first = terminal.multicell(0, 0)
            second = terminal.multicell(0, 2)
            self.assertTrue(first.valid)
            self.assertTrue(second.valid)
            self.assertEqual(first.column, 0)
            self.assertEqual(second.column, 0)
            self.assertEqual(terminal.snapshot().lines[0][:4], "A B ")


if __name__ == "__main__":
    unittest.main()
