# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

"""Public adaptations of the first 25 xterm.js ``BufferLine`` cases."""

import unittest

from harness import Shitty


PORTED_CASES = (
    "extended attributes: hasExtendedAttrs",
    "extended attributes: getUnderlineColor - P256",
    "extended attributes: getUnderlineColor - RGB",
    "extended attributes: underline color mode predicates",
    "extended attributes: getUnderlineStyle",
    "extended attributes: getUnderlineVariantOffset",
    "CellData: CharData equality",
    "BufferLine: ctor",
    "BufferLine: insertCells",
    "BufferLine: deleteCells",
    "BufferLine: replaceCells",
    "BufferLine: fill",
    "BufferLine: clone",
    "BufferLine: copyFrom",
    "BufferLine: combining chars",
    "BufferLine resize: enlarge(false)",
    "BufferLine resize: enlarge(true)",
    "BufferLine resize: shrink applies new size",
    "BufferLine resize: shrink to zero length",
    "BufferLine resize: removed combining data stays removed",
    "BufferLine getTrimmedLength: empty line",
    "BufferLine getTrimmedLength: ASCII",
    "BufferLine getTrimmedLength: surrogate",
    "BufferLine getTrimmedLength: combining",
    "BufferLine getTrimmedLength: fullwidth",
)

REMAINING_CASES = (
    "translateToString: empty line",
    "translateToString: ASCII",
    "translateToString: surrogate",
    "translateToString: combining",
    "translateToString: fullwidth",
    "translateToString: space at end",
    "translateToString: sane value for broken line",
    "translateToString: endCol zero",
    "addCodepointToCell: empty cell becomes width one",
    "addCodepointToCell: append to combining string",
    "addCodepointToCell: create combining string",
    "wide handling: insert at wide character",
    "wide handling: insert at end",
    "wide handling: delete",
    "wide handling: replace from zero",
    "wide handling: replace from one",
    "extended attributes: setCells",
    "extended attributes: loadCell",
    "extended attributes: fill",
    "extended attributes: insertCells",
    "extended attributes: deleteCells",
    "extended attributes: replaceCells",
    "extended attributes: clone",
    "extended attributes: copyFrom",
    "string cache: canonical translations",
    "string cache: mutation invalidation",
)

UPSTREAM_CASES = PORTED_CASES + REMAINING_CASES

# These assertions exercise xterm.js storage fields with no runtime producer.
# ``underlineVariantOffset`` is only assigned by xterm.js unit tests, and a
# zero-column BufferLine is not a valid terminal page. Their supported public
# portions (underline variants and minimum-size construction) remain covered
# below, but Shitty deliberately does not grow a test-only BufferLine API.
STORAGE_ONLY_DETAILS = (
    "extended attributes: getUnderlineVariantOffset",
    "BufferLine resize: shrink to zero length",
)


def trimmed_cell_length(snapshot, row=0):
    """Return the last materialized cell boundary, including wide tails."""
    result = 0
    for column in range(snapshot.columns):
        cell = snapshot.cell(column, row)
        if cell.drawn or cell.double_width_continuation:
            result = column + 1
    return result


class XtermJsBufferLineTest(unittest.TestCase):
    def test_upstream_inventory_has_51_distinct_cases(self):
        self.assertEqual(len(UPSTREAM_CASES), 51)
        self.assertEqual(len(set(UPSTREAM_CASES)), 51)
        self.assertEqual(len(PORTED_CASES), 25)
        self.assertTrue(set(STORAGE_ONLY_DETAILS) <= set(PORTED_CASES))

    def test_extended_attribute_presence_tracks_underline_state(self):
        with Shitty(columns=4, rows=2) as terminal:
            terminal.write(b"A\x1b[4:3;58;5;45mB\x1b[4:0;59mC")
            snapshot = terminal.model_snapshot()

            self.assertEqual(snapshot.cell(0, 0).underline_style, 0)
            self.assertEqual(snapshot.cell(1, 0).underline_style, 3)
            self.assertEqual(snapshot.cell(1, 0).underline_index, 45)
            self.assertEqual(snapshot.cell(2, 0).underline_style, 0)

    def test_palette_underline_color_uses_its_own_index(self):
        with Shitty(columns=4, rows=2) as terminal:
            terminal.write(b"\x1b[38;5;123;4;58;5;45mA\x1b[59mB")
            snapshot = terminal.model_snapshot()

            self.assertEqual(snapshot.cell(0, 0).foreground_index, 123)
            self.assertEqual(snapshot.cell(0, 0).underline_index, 45)
            self.assertEqual(
                snapshot.cell(1, 0).underline_color,
                snapshot.cell(1, 0).foreground,
            )

    def test_rgb_underline_color_uses_its_own_value(self):
        with Shitty(columns=4, rows=2) as terminal:
            terminal.write(
                b"\x1b[38;2;9;8;7;4;58;2;1;2;3mA\x1b[59mB"
            )
            snapshot = terminal.model_snapshot()

            self.assertEqual(snapshot.cell(0, 0).foreground, (9, 8, 7))
            self.assertEqual(snapshot.cell(0, 0).underline_color, (1, 2, 3))
            self.assertEqual(
                snapshot.cell(1, 0).underline_color,
                snapshot.cell(1, 0).foreground,
            )

    def test_underline_color_modes_and_default_fallback(self):
        with Shitty(columns=5, rows=2) as terminal:
            terminal.write(
                b"\x1b[4;38;5;123mA"
                b"\x1b[58;5;45mB"
                b"\x1b[58;2;1;2;3mC"
                b"\x1b[59mD"
            )
            snapshot = terminal.model_snapshot()

            self.assertEqual(snapshot.cell(0, 0).underline_index, 123)
            self.assertEqual(
                snapshot.cell(0, 0).underline_color,
                snapshot.cell(0, 0).foreground,
            )
            self.assertEqual(snapshot.cell(1, 0).underline_index, 45)
            self.assertEqual(snapshot.cell(2, 0).underline_index, -1)
            self.assertEqual(snapshot.cell(2, 0).underline_color, (1, 2, 3))
            self.assertEqual(
                snapshot.cell(3, 0).underline_color,
                snapshot.cell(3, 0).foreground,
            )

    def test_underline_style_requires_an_active_underline(self):
        with Shitty(columns=6, rows=2) as terminal:
            terminal.write(
                b"A\x1b[4mB\x1b[4:3mC\x1b[4:0mD"
                b"\x1b[4:5mE"
            )
            snapshot = terminal.model_snapshot()

            self.assertEqual(
                tuple(snapshot.cell(column, 0).underline_style for column in range(5)),
                (0, 1, 3, 0, 5),
            )

    def test_runtime_underline_variants_are_distinct(self):
        with Shitty(columns=6, rows=2) as terminal:
            terminal.write(b"".join(f"\x1b[4:{style}mX".encode() for style in range(1, 6)))
            snapshot = terminal.model_snapshot()

            self.assertEqual(
                tuple(snapshot.cell(column, 0).underline_style for column in range(5)),
                (1, 2, 3, 4, 5),
            )

    def test_cell_data_preserves_ascii_graphemes_surrogates_and_width(self):
        with Shitty(columns=12, rows=2) as terminal:
            terminal.write("ae\u0301𝄞𓂀\u0301１".encode())
            snapshot = terminal.model_snapshot()

            self.assertEqual(snapshot.cell(0, 0).char, "a")
            self.assertEqual(snapshot.cell(1, 0).grapheme, (ord("e"), 0x301))
            self.assertEqual(snapshot.cell(2, 0).char, "𝄞")
            self.assertEqual(snapshot.cell(3, 0).grapheme, (0x13080, 0x301))
            self.assertTrue(snapshot.cell(4, 0).double_width)
            self.assertTrue(snapshot.cell(5, 0).double_width_continuation)

    def test_new_line_has_requested_width_and_blank_cells(self):
        with Shitty(columns=10, rows=2) as terminal:
            snapshot = terminal.model_snapshot()

            self.assertEqual((snapshot.columns, snapshot.rows), (10, 2))
            self.assertEqual(snapshot.lines, [" " * 10, " " * 10])
            self.assertFalse(any(cell.drawn for cell in snapshot.cells))
            self.assertFalse(any(cell.wrapped for cell in snapshot.cells))

    def test_ich_inserts_erased_cells_and_shifts_the_tail(self):
        with Shitty(columns=5, rows=2) as terminal:
            terminal.write(b"abcde\x1b[1;2H\x1b[2@")
            self.assertEqual(terminal.snapshot().lines[0], "a  bc")

    def test_dch_deletes_cells_shifts_left_and_erases_the_tail(self):
        with Shitty(columns=5, rows=2) as terminal:
            terminal.write(b"abcde\x1b[1;2H\x1b[2P")
            self.assertEqual(terminal.snapshot().lines[0], "ade  ")

    def test_ech_replaces_only_the_requested_cells(self):
        with Shitty(columns=5, rows=2) as terminal:
            terminal.write(b"abcde\x1b[1;3H\x1b[2X")
            self.assertEqual(terminal.snapshot().lines[0], "ab  e")

    def test_erased_fill_cells_keep_the_current_background(self):
        with Shitty(columns=5, rows=2) as terminal:
            terminal.write(b"abcde\x1b[48;5;21m\x1b[2J")
            snapshot = terminal.model_snapshot()

            self.assertEqual(snapshot.lines, [" " * 5, " " * 5])
            self.assertTrue(all(cell.background_index == 21 for cell in snapshot.cells))

    def test_line_state_survives_resize_clone_path(self):
        with Shitty(columns=5, rows=2) as terminal:
            terminal.write(b"\x1b[4:3;58;5;45mabc")
            before = terminal.model_snapshot()
            terminal.resize(8, 2)
            after = terminal.model_snapshot()

            for column in range(3):
                self.assertEqual(after.cell(column, 0), before.cell(column, 0))

    def test_line_state_survives_reflow_copy_path(self):
        with Shitty(columns=4, rows=3) as terminal:
            terminal.write(b"\x1b[38;5;123mabcdef")
            terminal.resize(3, 3)
            snapshot = terminal.model_snapshot()

            self.assertEqual("".join(line.rstrip() for line in snapshot.lines), "abcdef")
            for row, column in ((0, 0), (0, 1), (0, 2), (1, 0), (1, 1), (1, 2)):
                self.assertEqual(snapshot.cell(column, row).foreground_index, 123)

    def test_combining_graphemes_survive_copy_and_resize(self):
        with Shitty(columns=4, rows=3) as terminal:
            terminal.write("e\u0301e\u0301e\u0301e\u0301e\u0301".encode())
            terminal.resize(3, 3)
            terminal.resize(5, 3)
            snapshot = terminal.model_snapshot()

            graphemes = [cell.grapheme for cell in snapshot.cells if cell.drawn]
            self.assertEqual(graphemes, [(ord("e"), 0x301)] * 5)

    def test_resize_enlarges_a_hard_line_with_blank_cells(self):
        with Shitty(columns=5, rows=2) as terminal:
            terminal.write(b"aaaaa\x1b[2;1H")
            terminal.resize(10, 2)
            self.assertEqual(terminal.snapshot().lines[0], "aaaaa" + " " * 5)

    def test_resize_enlarges_and_unwraps_a_soft_line(self):
        with Shitty(columns=5, rows=3) as terminal:
            terminal.write(b"abcdefghij")
            terminal.resize(10, 3)
            snapshot = terminal.snapshot()

            self.assertEqual(snapshot.lines[0], "abcdefghij")
            self.assertFalse(any(cell.wrapped for cell in snapshot.cells[:10]))

    def test_resize_shrinks_a_line_to_the_new_width(self):
        with Shitty(columns=10, rows=3) as terminal:
            terminal.write(b"aaaaaaaaaa\x1b[2;1H")
            terminal.resize(5, 3)
            self.assertEqual(terminal.all_text()[:2], ("aaaaa", "aaaaa"))

    def test_terminal_grid_clamps_to_a_nonzero_public_size(self):
        with Shitty(columns=2, rows=2) as terminal:
            terminal.resize(1, 1)
            snapshot = terminal.model_snapshot()
            self.assertEqual((snapshot.columns, snapshot.rows), (1, 1))

    def test_removed_combining_data_does_not_reappear_after_growth(self):
        with Shitty(columns=10, rows=2) as terminal:
            terminal.write("aaе\u0301\x1b[1;9Hе\u0301".encode())
            terminal.write(b"\x1b[1;9H\x1b[X")
            terminal.resize(5, 2)
            terminal.resize(10, 2)
            snapshot = terminal.model_snapshot()
            graphemes = [cell.grapheme for cell in snapshot.cells if cell.drawn]

            self.assertEqual(graphemes.count((ord("е"), 0x301)), 1)

    def test_empty_line_has_zero_trimmed_cell_length(self):
        with Shitty(columns=10, rows=2) as terminal:
            self.assertEqual(trimmed_cell_length(terminal.model_snapshot()), 0)

    def test_ascii_trimmed_length_ends_after_last_drawn_cell(self):
        with Shitty(columns=10, rows=2) as terminal:
            terminal.write(b"a\x1b[1;3Ha")
            self.assertEqual(trimmed_cell_length(terminal.model_snapshot()), 3)

    def test_surrogate_trimmed_length_counts_one_cell(self):
        with Shitty(columns=10, rows=2) as terminal:
            terminal.write("a\x1b[1;3H𝄞".encode())
            self.assertEqual(trimmed_cell_length(terminal.model_snapshot()), 3)

    def test_combining_trimmed_length_counts_one_grapheme_cell(self):
        with Shitty(columns=10, rows=2) as terminal:
            terminal.write("a\x1b[1;3He\u0301".encode())
            snapshot = terminal.model_snapshot()

            self.assertEqual(trimmed_cell_length(snapshot), 3)
            self.assertEqual(snapshot.cell(2, 0).grapheme, (ord("e"), 0x301))

    def test_fullwidth_trimmed_length_includes_the_continuation_cell(self):
        with Shitty(columns=10, rows=2) as terminal:
            terminal.write("a\x1b[1;3H１".encode())
            snapshot = terminal.model_snapshot()

            self.assertEqual(trimmed_cell_length(snapshot), 4)
            self.assertTrue(snapshot.cell(2, 0).double_width)
            self.assertTrue(snapshot.cell(3, 0).double_width_continuation)


if __name__ == "__main__":
    unittest.main()
