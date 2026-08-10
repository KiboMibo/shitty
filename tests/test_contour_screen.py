# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import unittest

from harness import Shitty


UPSTREAM_CASES = (
    "writeText.bulk.A.1",
    "writeText.bulk.A.2",
    "writeText.bulk.A.3",
    "writeText.autowrap.threeIdenticalFullLines",
    "writeText.bulk.B",
    "writeText.bulk.C",
    "writeText.bulk.D",
    "writeText.bulk.E",
    "writeText.bulk.F",
    "writeText.bulk.G",
    "writeText.bulk.H",
    "AppendChar",
)

UNICODE_UPSTREAM_CASES = (
    "AppendChar_CR_LF",
    "AppendChar.emoji_exclamationmark",
    "AppendChar.VS15_selects_text_presentation_without_changing_the_width",
    "AppendChar.VS15_is_inert_without_a_defined_variation_sequence",
    "AppendChar.a_zero_width_codepoint_after_an_ASCII_base_joins_its_cell",
    "AppendChar.a_wide_char_at_the_second_to_last_column_claims_the_last_one",
    "AppendChar.overwriting_a_wide_char_at_the_second_to_last_column_clears_its_tail",
    "AppendChar.a_wide_char_still_wraps_when_only_one_column_is_left",
    "AppendChar.emoji_VS16_copyright_sign",
    "AppendChar.width_revision_is_gated_on_mode_2027",
    "AppendChar.width_revision_resumes_when_2027_is_set_again",
    "AppendChar.width_revision_at_right_edge_keeps_cursor_on_page",
    "AppendChar.abandoned_width_revision_restores_the_head_cell",
    "Screen.copyArea_does_not_remeasure_cluster_widths",
    "AppendChar.emoji_VS16_i",
    "AppendChar.emoji_family",
    "AppendChar.emoji_zwj_1",
    "AppendChar.emoji_zwj_ten_codepoints",
    "AppendChar.emoji_1",
    "AppendChar_WideChar",
    "AppendChar_Into_WideChar_Right_Half",
    "AppendChar_AutoWrap",
    "AppendChar_AutoWrap_LF",
)

VIEWPORT_ERASE_UPSTREAM_CASES = (
    "Screen.isLineVisible",
    "Backspace",
    "Linefeed",
    "DSR.Unsolicited_ColorPaletteUpdated",
    "ClearToEndOfScreen",
    "ClearToBeginOfScreen",
    "ClearScreen",
    "ClearToEndOfLine",
    "ClearToBeginOfLine",
    "ClearLine",
    "DECFI",
    "InsertColumns",
)

EDITING_PROTECTION_UPSTREAM_CASES = (
    "InsertCharacters.NoMargins",
    "InsertCharacters.Margins",
    "InsertMode",
    "InsertLines",
    "DECSCA: enable and disable character protection",
    "DECSCA: default parameter disables protection",
    "DECSCA: protection is independent of SGR rendition",
    "DECSCA: save and restore cursor preserves protection state",
    "DECSEL-0",
    "DECSEL-1",
    "DECSEL-2",
    "DECSED-0",
)


class ContourScreenTest(unittest.TestCase):
    def test_upstream_inventory_has_all_12_cases(self):
        self.assertEqual(len(UPSTREAM_CASES), 12)
        self.assertEqual(len(set(UPSTREAM_CASES)), 12)

    def test_unicode_inventory_has_all_23_cases(self):
        self.assertEqual(len(UNICODE_UPSTREAM_CASES), 23)
        self.assertEqual(len(set(UNICODE_UPSTREAM_CASES)), 23)

    def test_viewport_erase_inventory_has_all_12_cases(self):
        self.assertEqual(len(VIEWPORT_ERASE_UPSTREAM_CASES), 12)
        self.assertEqual(len(set(VIEWPORT_ERASE_UPSTREAM_CASES)), 12)

    def test_editing_protection_inventory_has_all_12_cases(self):
        self.assertEqual(len(EDITING_PROTECTION_UPSTREAM_CASES), 12)
        self.assertEqual(len(set(EDITING_PROTECTION_UPSTREAM_CASES)), 12)

    def test_bulk_text_with_autowrap_disabled(self):
        for suffix, expected in (
            (b"CD", "abCD "),
            (b"CDE", "abCDE"),
            (b"CDEF", "abCDF"),
        ):
            with self.subTest(suffix=suffix), Shitty(
                columns=5,
                rows=3,
                save_lines=2,
            ) as terminal:
                terminal.write_chunks(b"\x1b[?7l", b"a", b"b", suffix)
                snapshot = terminal.snapshot()
                self.assertEqual(snapshot.lines[:2], [expected, "     "])
                self.assertEqual(
                    (snapshot.cursor_x, snapshot.cursor_y),
                    (4, 0),
                )

    def test_autowrap_fills_three_identical_lines_without_a_gap(self):
        with Shitty(columns=10, rows=6) as terminal:
            terminal.write(b"\x1b[H\x1b[?7h")
            terminal.write_chunks(*(b"*" for _ in range(20)))
            terminal.write(b"\x1b[?7l\x1b[3;1H")
            terminal.write_chunks(*(b"*" for _ in range(20)))
            terminal.write(b"\x1b[?7h")

            self.assertEqual(
                terminal.snapshot().lines[:4],
                ["*" * 10, "*" * 10, "*" * 10, " " * 10],
            )

    def test_bulk_text_stops_before_and_exactly_at_right_edge(self):
        for suffix, expected, cursor_x, pending in (
            (b"CD", "abCD ", 4, False),
            (b"CDE", "abCDE", 4, True),
        ):
            with self.subTest(suffix=suffix), Shitty(
                columns=5,
                rows=3,
                save_lines=2,
            ) as terminal:
                terminal.write_chunks(b"a", b"b", suffix)
                snapshot = terminal.snapshot()
                self.assertEqual(snapshot.lines[0], expected)
                self.assertEqual(
                    (snapshot.cursor_x, snapshot.cursor_y),
                    (cursor_x, 0),
                )
                self.assertEqual(terminal.cursor_pending_wrap(), pending)

                if pending:
                    terminal.write(b"F")
                    snapshot = terminal.snapshot()
                    self.assertEqual(snapshot.lines[:2], ["abCDE", "F    "])
                    self.assertEqual(
                        (snapshot.cursor_x, snapshot.cursor_y),
                        (1, 1),
                    )

    def test_bulk_text_wraps_across_page_and_history(self):
        cases = (
            (
                3,
                10,
                1,
                b"abCDEFGHIJABcdefghij01234",
                ("abCDEFGHIJ", "ABcdefghij", "01234"),
                (5, 2),
                False,
            ),
            (
                3,
                10,
                1,
                b"abCDEFGHIJABCDEFGHIJabcdefghij01234",
                ("abCDEFGHIJ", "ABCDEFGHIJ", "abcdefghij", "01234"),
                (5, 2),
                False,
            ),
            (
                2,
                10,
                1,
                b"ABCDEFGHIJKLMNOPQRSTabcdefghij0123456789",
                (
                    "ABCDEFGHIJ",
                    "KLMNOPQRST",
                    "abcdefghij",
                    "0123456789",
                ),
                (9, 1),
                True,
            ),
        )
        for rows, columns, history, text, expected, cursor, pending in cases:
            with self.subTest(text=text), Shitty(
                columns=columns,
                rows=rows,
                save_lines=history,
            ) as terminal:
                terminal.write(text)
                self.assertEqual(terminal.all_text(), expected)
                snapshot = terminal.snapshot()
                self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), cursor)
                self.assertEqual(terminal.cursor_pending_wrap(), pending)

    def test_full_page_defers_scroll_until_the_next_character(self):
        with Shitty(columns=10, rows=3, save_lines=2) as terminal:
            terminal.write(
                b"0123456789"
                b"abcdefghij"
                b"ABCDEFGHIJ"
            )
            snapshot = terminal.snapshot()
            self.assertEqual(
                snapshot.lines,
                ["0123456789", "abcdefghij", "ABCDEFGHIJ"],
            )
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (9, 2))
            self.assertTrue(terminal.cursor_pending_wrap())

            terminal.write(b"X")
            self.assertEqual(
                terminal.all_text(),
                ("0123456789", "abcdefghij", "ABCDEFGHIJ", "X"),
            )
            self.assertEqual(
                terminal.snapshot().lines,
                ["abcdefghij", "ABCDEFGHIJ", "X         "],
            )

    def test_enabling_autowrap_preserves_the_right_edge_position(self):
        with Shitty(columns=3, rows=1, save_lines=1) as terminal:
            terminal.write(b"\x1b[?7l")
            terminal.write_chunks(b"A", b"B", b"C", b"D")
            self.assertEqual(terminal.snapshot().lines, ["ABD"])

            terminal.write(b"\x1b[?7hE")
            self.assertEqual(terminal.snapshot().lines, ["ABE"])
            self.assertTrue(terminal.cursor_pending_wrap())

            terminal.write(b"F")
            self.assertEqual(terminal.all_text(), ("ABE", "F"))
            self.assertEqual(terminal.snapshot().lines, ["F  "])

    def test_cr_lf_with_autowrap_disabled(self):
        with Shitty(columns=3, rows=2) as terminal:
            terminal.write(b"\x1b[?7lABC")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, ["ABC", "   "])
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (2, 0))

            terminal.write(b"\r")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, ["ABC", "   "])
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 0))

            terminal.write(b"\n")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, ["ABC", "   "])
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 1))

    def test_mode_2027_gates_streaming_cluster_width_revision(self):
        information_emoji = "ℹ️".encode()
        with Shitty(columns=5, rows=2) as terminal:
            terminal.write(b"\x1b[?2027l" + information_emoji)
            snapshot = terminal.model_snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (1, 0))
            self.assertEqual(
                snapshot.cell(0, 0).grapheme,
                (0x2139, 0xFE0F),
            )
            self.assertFalse(snapshot.cell(0, 0).double_width)

            terminal.write(b"\x1b[?2027h\x1b[2;1H" + information_emoji)
            snapshot = terminal.model_snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (2, 1))
            self.assertEqual(
                snapshot.cell(0, 1).grapheme,
                (0x2139, 0xFE0F),
            )
            self.assertTrue(snapshot.cell(0, 1).double_width)
            self.assertTrue(snapshot.cell(1, 1).double_width_continuation)

    def test_copy_rectangle_does_not_remeasure_cluster_width(self):
        with Shitty(columns=8, rows=4) as terminal:
            terminal.write(b"\x1b[?2027l" + "ℹ️".encode() + b"X")
            terminal.write(b"\x1b[?2027h\x1b[1;1;1;2;1;3;1;1$v")
            snapshot = terminal.model_snapshot()

            self.assertEqual(snapshot.cell(0, 2).grapheme, (0x2139, 0xFE0F))
            self.assertFalse(snapshot.cell(0, 2).double_width)
            self.assertFalse(snapshot.cell(1, 2).double_width_continuation)
            self.assertEqual(snapshot.cell(1, 2).char, "X")


if __name__ == "__main__":
    unittest.main()
