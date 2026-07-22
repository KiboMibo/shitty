import unittest

from harness import Zutty, put_rows


def assert_wide_cells_are_complete(test, snapshot):
    for row in range(snapshot.rows):
        for column in range(snapshot.columns):
            cell = snapshot.cell(column, row)
            if cell.double_width:
                test.assertLess(column + 1, snapshot.columns)
                test.assertTrue(
                    snapshot.cell(column + 1, row).double_width_continuation
                )
            if cell.double_width_continuation:
                test.assertGreater(column, 0)
                test.assertTrue(snapshot.cell(column - 1, row).double_width)


class EditingMatrixTest(unittest.TestCase):
    def test_large_character_edit_counts_clip_at_right_edge(self):
        for operation in (b"@", b"P", b"X"):
            with self.subTest(operation=operation):
                with Zutty(columns=6, rows=2) as terminal:
                    terminal.write(b"abcdef\x1b[1;3H\x1b[4294967295" + operation)
                    self.assertEqual(terminal.snapshot().lines[0], "ab    ")

    def test_large_line_edit_counts_clip_at_bottom_margin(self):
        for operation in (b"L", b"M"):
            with self.subTest(operation=operation):
                with Zutty(columns=5, rows=4) as terminal:
                    terminal.write(put_rows(b"one", b"two", b"three", b"four"))
                    terminal.write(b"\x1b[2;1H\x1b[4294967295" + operation)
                    self.assertEqual(
                        terminal.snapshot().lines,
                        ["one  ", "     ", "     ", "     "],
                    )

    def test_large_vertical_scroll_counts_clear_region(self):
        for operation in (b"S", b"T"):
            with self.subTest(operation=operation):
                with Zutty(columns=5, rows=4) as terminal:
                    terminal.write(put_rows(b"one", b"two", b"three", b"four"))
                    terminal.write(b"\x1b[2;4r\x1b[65536" + operation)
                    self.assertEqual(
                        terminal.snapshot().lines,
                        ["one  ", "     ", "     ", "     "],
                    )

    def test_large_horizontal_scroll_counts_clear_margins(self):
        for operation in (b" @", b" A"):
            with self.subTest(operation=operation):
                with Zutty(columns=6, rows=2) as terminal:
                    terminal.write(put_rows(b"abcdef", b"ghijkl"))
                    terminal.write(
                        b"\x1b[?69h\x1b[2;5s\x1b[1;2H\x1b[4294967295"
                        + operation
                    )
                    self.assertEqual(terminal.snapshot().lines, ["a    f", "g    l"])

    def test_horizontal_scroll_requires_cursor_inside_vertical_margins(self):
        cases = (
            (b" @", ["abcdef", "ijkl  ", "opqr  ", "stuvwx"]),
            (b" A", ["abcdef", "  ghij", "  mnop", "stuvwx"]),
        )
        for operation, expected in cases:
            with self.subTest(operation=operation):
                with Zutty(columns=6, rows=4) as terminal:
                    original = ["abcdef", "ghijkl", "mnopqr", "stuvwx"]
                    terminal.write(put_rows(*(row.encode() for row in original)))
                    terminal.write(b"\x1b[2;3r\x1b[1;1H\x1b[2" + operation)
                    self.assertEqual(terminal.snapshot().lines, original)
                    terminal.write(b"\x1b[2;1H\x1b[2" + operation)
                    self.assertEqual(terminal.snapshot().lines, expected)
                    terminal.write(b"\x1b[4;1H\x1b[2" + operation)
                    self.assertEqual(terminal.snapshot().lines, expected)

    def test_large_column_edit_counts_clear_to_right_margin(self):
        for operation in (b"'}", b"'~"):
            with self.subTest(operation=operation):
                with Zutty(columns=6, rows=2) as terminal:
                    terminal.write(put_rows(b"abcdef", b"ghijkl"))
                    terminal.write(
                        b"\x1b[?69h\x1b[2;5s\x1b[1;3H\x1b[4294967295" + operation
                    )
                    self.assertEqual(terminal.snapshot().lines, ["ab   f", "gh   l"])

    def test_large_repeat_count_is_bounded_by_observable_history(self):
        with Zutty(columns=6, rows=2, save_lines=0) as terminal:
            terminal.write(b"A\x1b[65536b")
            self.assertEqual(terminal.snapshot().lines, ["AAAAAA", "AAAAA "])

    def test_invalid_overflowed_vertical_margins_keep_previous_region(self):
        with Zutty(columns=5, rows=4) as terminal:
            terminal.write(put_rows(b"one", b"two", b"three", b"four"))
            terminal.write(b"\x1b[2;4r\x1b[65537;4r\x1b[S")
            self.assertEqual(
                terminal.snapshot().lines,
                ["one  ", "three", "four ", "     "],
            )

    def test_invalid_overflowed_horizontal_margins_keep_previous_region(self):
        with Zutty(columns=6, rows=2) as terminal:
            terminal.write(put_rows(b"abcdef", b"ghijkl"))
            terminal.write(
                b"\x1b[?69h\x1b[2;5s\x1b[65537;6s\x1b[1;2H\x1b[ @"
            )
            self.assertEqual(terminal.snapshot().lines, ["acde f", "gijk l"])

    def test_line_editing_outside_margins_is_ignored(self):
        for operation in (b"L", b"M"):
            with self.subTest(operation=operation):
                with Zutty(columns=5, rows=4) as terminal:
                    terminal.write(put_rows(b"one", b"two", b"three", b"four"))
                    terminal.write(b"\x1b[2;4r\x1b[1;1H\x1b[" + operation)
                    self.assertEqual(
                        terminal.snapshot().lines,
                        ["one  ", "two  ", "three", "four "],
                    )

    def test_rectangular_copy_handles_overlap_in_every_direction(self):
        cases = (
            (b"\x1b[1;1;2;3;1;2;3;1$v", ["abcdef", "ghabcl", "mnghir"]),
            (b"\x1b[2;3;3;5;1;1;1;1$v", ["ijkdef", "opqjkl", "mnopqr"]),
            (b"\x1b[1;1;1;4;1;1;3;1$v", ["ababcd", "ghijkl", "mnopqr"]),
            (b"\x1b[1;3;1;6;1;1;1;1$v", ["cdefef", "ghijkl", "mnopqr"]),
        )
        for sequence, expected in cases:
            with self.subTest(sequence=sequence):
                with Zutty(columns=6, rows=3) as terminal:
                    terminal.write(put_rows(b"abcdef", b"ghijkl", b"mnopqr"))
                    terminal.write(sequence)
                    self.assertEqual(terminal.snapshot().lines, expected)

    def test_reversed_rectangular_coordinates_are_ignored(self):
        sequences = (
            b"\x1b[88;2;4;1;2$x",
            b"\x1b[2;4;1;2$z",
            b"\x1b[2;4;1;2${",
            b"\x1b[2;4;1;2;1;1;1;1$v",
            b"\x1b[2;4;1;2;1$r",
            b"\x1b[2;4;1;2;1$t",
        )
        for sequence in sequences:
            with self.subTest(sequence=sequence):
                with Zutty(columns=6, rows=2) as terminal:
                    terminal.write(put_rows(b"abcdef", b"ghijkl"))
                    terminal.write(sequence)
                    self.assertEqual(terminal.snapshot().lines, ["abcdef", "ghijkl"])

    def test_rectangular_copy_clips_destination_at_page_edges(self):
        with Zutty(columns=5, rows=3) as terminal:
            terminal.write(put_rows(b"abcde", b"fghij", b"klmno"))
            terminal.write(b"\x1b[1;1;2;3;1;3;4;1$v")
            self.assertEqual(terminal.snapshot().lines, ["abcde", "fghij", "klmab"])

    def test_rectangular_page_numbers_resolve_to_only_available_page(self):
        with Zutty(columns=5, rows=2) as terminal:
            terminal.write(put_rows(b"abcde", b"fghij"))
            terminal.write(b"\x1b[1;1;1;2;99;2;3;99$v")
            self.assertEqual(terminal.snapshot().lines, ["abcde", "fgabj"])

    def test_rectangular_copy_keeps_destination_line_attributes(self):
        with Zutty(columns=5, rows=2) as terminal:
            terminal.write(b"abcde\x1b[2;1H\x1b#6")
            terminal.write(b"\x1b[1;1;1;2;1;2;1;1$v")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(0, 1).line_attribute, 3)
            self.assertEqual(snapshot.cell(1, 1).line_attribute, 3)

    def test_rectangular_operations_obey_origin_mode(self):
        with Zutty(columns=7, rows=4) as terminal:
            terminal.write(b"\x1b[2;4r\x1b[?69h\x1b[3;6s\x1b[?6h")
            terminal.write(b"\x1b[88;1;1;1;1$x")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(2, 1).char, "X")
            self.assertEqual(snapshot.cell(0, 0).char, " ")

    def test_selective_rectangular_erase_preserves_protection(self):
        with Zutty(columns=6, rows=2) as terminal:
            terminal.write(b"a\x1b[1\"qBC\x1b[0\"qdef")
            terminal.write(b"\x1b[1;1;1;6${")
            self.assertEqual(terminal.snapshot().lines[0], " BC   ")

    def test_rectangular_changes_cover_all_attributes_and_resets(self):
        with Zutty(columns=3, rows=1) as terminal:
            terminal.write(b"abc\x1b[1;1;1;3;1;4;5;7;8$r")
            cell = terminal.snapshot().cell(0, 0)
            self.assertTrue(cell.bold)
            self.assertTrue(cell.underline)
            self.assertTrue(cell.blink)
            self.assertTrue(cell.inverse)
            self.assertTrue(cell.conceal)
            terminal.write(b"\x1b[1;1;1;3;22;24;25;27;28$r")
            cell = terminal.snapshot().cell(0, 0)
            self.assertFalse(cell.bold)
            self.assertFalse(cell.underline)
            self.assertFalse(cell.blink)
            self.assertFalse(cell.inverse)
            self.assertFalse(cell.conceal)

    def test_rectangular_edits_never_leave_split_wide_cells(self):
        sequences = (
            b"\x1b[88;1;3;1;3$x",
            b"\x1b[1;2;1;2$z",
            b"\x1b[1;2;1;2;1;2;5;1$v",
        )
        for sequence in sequences:
            with self.subTest(sequence=sequence):
                with Zutty(columns=7, rows=2) as terminal:
                    terminal.write("A界BC".encode())
                    terminal.write(sequence)
                    assert_wide_cells_are_complete(self, terminal.snapshot())


if __name__ == "__main__":
    unittest.main()
