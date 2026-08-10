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

ISO_PROTECTION_UPSTREAM_CASES = (
    "DECSED-1",
    "DECSED-2",
    "DECSED-2: lines without protected characters are erased correctly",
    "SPA/EPA: ED respects ISO protection",
    "SPA/EPA: EL respects ISO protection",
    "SPA/EPA: ECH respects ISO protection",
    "SPA/EPA: 8-bit C1 forms behave like ESC V / ESC W",
    "SPA/EPA: 8-bit C1 protection survives inside a coalesced text run",
    "DECSCA: regular ED does not respect DEC protection",
    "SPA/EPA: soft reset clears ISO protection mode",
    "SPA/EPA: selective erases do NOT respect ISO protection",
    "DECSCA: selective erase still respects DEC protection after the ISO split",
)

VT52_RECTANGLE_UPSTREAM_CASES = (
    "VT52: enter, cursor movement, and leave",
    "VT52: identify responds with ESC / Z",
    "VT52: erase to end of line and screen",
    "DECSERA-all-defaults",
    "DECSERA",
    "DeleteLines",
    "DECFRA",
    "DECFRA.Vertical",
    "DECFRA.Horizontal",
    "DECFRA.Invalid",
    "DECFRA.Default",
    "DECFRA.Full",
)

EDIT_SCROLL_CURSOR_UPSTREAM_CASES = (
    "DeleteColumns",
    "DeleteCharacters",
    "ClearScrollbackBuffer",
    "EraseCharacters",
    "ScrollUp.WithMargins",
    "ScrollUp",
    "ScrollDown",
    "Unscroll",
    "Sequence.CUU",
    "MoveCursorDown",
    "MoveCursorForward",
    "MoveCursorBackward",
)

POSITION_MARGIN_UPSTREAM_CASES = (
    "HorizontalPositionAbsolute",
    "HorizontalPositionRelative",
    "MoveCursorToColumn",
    "MoveCursorToLine",
    "MoveCursorToBeginOfLine",
    "CarriageReturn_honours_left_margin",
    "NEL_indexes_and_returns_to_margin",
    "SD_respects_left_right_margin",
    "IL_over_region_clears_the_band",
    "Autowrap_within_left_right_margin",
    "DECBI_back_index",
    "CNL_CPL_clamp_to_scroll_region_and_left_margin",
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

    def test_iso_protection_inventory_has_all_12_cases(self):
        self.assertEqual(len(ISO_PROTECTION_UPSTREAM_CASES), 12)
        self.assertEqual(len(set(ISO_PROTECTION_UPSTREAM_CASES)), 12)

    def test_vt52_rectangle_inventory_has_all_12_cases(self):
        self.assertEqual(len(VT52_RECTANGLE_UPSTREAM_CASES), 12)
        self.assertEqual(len(set(VT52_RECTANGLE_UPSTREAM_CASES)), 12)

    def test_vt52_enter_cursor_movement_and_leave(self):
        with Shitty(columns=10, rows=5) as terminal:
            terminal.write(b"\x1b[?2l\x1bY\x23\x25")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (5, 3))

            terminal.write(b"\x1bH\x1bB\x1bB\x1bC")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (1, 2))

            terminal.write(b"\x1bA\x1bD")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 1))

            terminal.write(b"\x1b<\x1b[3;4H")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (3, 2))

    def test_vt52_identify_responds_with_escape_slash_z(self):
        with Shitty(columns=4, rows=2) as terminal:
            terminal.write(b"\x1b[?2l\x1bZ")
            self.assertEqual(terminal.read_input(), b"\x1b/Z")

    def test_vt52_erase_to_end_of_line(self):
        with Shitty(columns=4, rows=2) as terminal:
            terminal.write(b"abcd\x1b[?2l\x1bY\x20\x22\x1bK")
            self.assertEqual(terminal.snapshot().lines[0], "ab  ")

    def test_decsera_all_defaults(self):
        with Shitty(columns=3, rows=3) as terminal:
            terminal.write(
                b"\x1b[1\"qA\x1b[2\"qB\x1b[1\"qC\x1b[2\"q\r\n"
                b"D\x1b[1\"qE\x1b[2\"qF\r\n"
                b"\x1b[1\"qG\x1b[2\"qH\x1b[1\"qI\x1b[2\"q"
                b"\x1b[${"
            )
            self.assertEqual(terminal.snapshot().lines, ["A C", " E ", "G I"])

    def test_decsera_explicit_rectangle(self):
        with Shitty(columns=3, rows=3) as terminal:
            terminal.write(
                b"\x1b[1\"qA\x1b[2\"qB\x1b[1\"qC\x1b[2\"q\r\n"
                b"D\x1b[1\"qE\x1b[2\"qF\r\n"
                b"\x1b[1\"qG\x1b[2\"qH\x1b[1\"qI\x1b[2\"q"
                b"\x1b[2;2;3;3${"
            )
            self.assertEqual(terminal.snapshot().lines, ["ABC", "DE ", "G I"])

    def test_delete_lines_contour_scenario(self):
        page = b"AB\r\nCD\r\nEF\x1b[2;1H"
        for count, expected in (
            (1, ["AB", "EF", "  "]),
            (5, ["AB", "  ", "  "]),
        ):
            with self.subTest(count=count), Shitty(columns=2, rows=3) as terminal:
                terminal.write(page + f"\x1b[{count}M".encode())
                self.assertEqual(terminal.snapshot().lines, expected)

    def test_decfra_rectangle(self):
        with Shitty(columns=5, rows=5) as terminal:
            terminal.write(
                b"12345\r\n67890\r\nABCDE\r\nFGHIJ\r\nKLMNO"
                b"\x1b[46;2;2;4;4$x"
            )
            self.assertEqual(
                terminal.snapshot().lines,
                ["12345", "6...0", "A...E", "F...J", "KLMNO"],
            )

    def test_decfra_vertical_rectangle(self):
        with Shitty(columns=5, rows=5) as terminal:
            terminal.write(
                b"12345\r\n67890\r\nABCDE\r\nFGHIJ\r\nKLMNO"
                b"\x1b[46;3;1;3;5$x"
            )
            self.assertEqual(
                terminal.snapshot().lines,
                ["12345", "67890", ".....", "FGHIJ", "KLMNO"],
            )

    def test_decfra_horizontal_rectangle(self):
        with Shitty(columns=5, rows=5) as terminal:
            terminal.write(
                b"12345\r\n67890\r\nABCDE\r\nFGHIJ\r\nKLMNO"
                b"\x1b[46;1;3;5;3$x"
            )
            self.assertEqual(
                terminal.snapshot().lines,
                ["12.45", "67.90", "AB.DE", "FG.IJ", "KL.NO"],
            )

    def test_decfra_zero_edges_default_to_page_edges(self):
        with Shitty(columns=5, rows=5) as terminal:
            terminal.write(
                b"12345\r\n67890\r\nABCDE\r\nFGHIJ\r\nKLMNO"
                b"\x1b[46;0;0;5;5$x"
            )
            self.assertEqual(terminal.snapshot().lines, ["....."] * 5)

    def test_decfra_omitted_edges_default_to_page_edges(self):
        with Shitty(columns=5, rows=5) as terminal:
            terminal.write(
                b"12345\r\n67890\r\nABCDE\r\nFGHIJ\r\nKLMNO"
                b"\x1b[46$x"
            )
            self.assertEqual(terminal.snapshot().lines, ["....."] * 5)

    def test_decfra_explicit_full_page(self):
        with Shitty(columns=5, rows=5) as terminal:
            terminal.write(
                b"12345\r\n67890\r\nABCDE\r\nFGHIJ\r\nKLMNO"
                b"\x1b[46;1;1;5;5$x"
            )
            self.assertEqual(terminal.snapshot().lines, ["....."] * 5)

    def test_edit_scroll_cursor_inventory_has_all_12_cases(self):
        self.assertEqual(len(EDIT_SCROLL_CURSOR_UPSTREAM_CASES), 12)
        self.assertEqual(len(set(EDIT_SCROLL_CURSOR_UPSTREAM_CASES)), 12)

    def test_position_margin_inventory_has_all_12_cases(self):
        self.assertEqual(len(POSITION_MARGIN_UPSTREAM_CASES), 12)
        self.assertEqual(len(set(POSITION_MARGIN_UPSTREAM_CASES)), 12)

    def test_delete_columns(self):
        page = b"12345\r\n67890\r\nABCDE\r\nFGHIJ\r\nKLMNO"
        for count, expected in (
            (1, ["12345", "679 0", "ABD E", "FGI J", "KLMNO"]),
            (2, ["12345", "67  0", "AB  E", "FG  J", "KLMNO"]),
            (4, ["12345", "67  0", "AB  E", "FG  J", "KLMNO"]),
        ):
            with self.subTest(count=count), Shitty(columns=5, rows=5) as terminal:
                terminal.write(page)
                terminal.write(b"\x1b[?69h\x1b[2;4s\x1b[2;4r\x1b[2;3H")
                terminal.write(f"\x1b[{count}'~".encode())
                self.assertEqual(terminal.snapshot().lines, expected)

        with Shitty(columns=5, rows=5) as terminal:
            terminal.write(page)
            terminal.write(b"\x1b[?69h\x1b[2;4s\x1b[2;4r\x1b[5;5H\x1b[1'~")
            self.assertEqual(
                terminal.snapshot().lines,
                ["12345", "67890", "ABCDE", "FGHIJ", "KLMNO"],
            )

    def test_delete_characters(self):
        page = b"12345\r\n67890"
        for count, expected in (
            (1, "1345 "),
            (2, "145  "),
            (4, "1    "),
            (5, "1    "),
        ):
            with self.subTest(margins=False, count=count), Shitty(
                columns=5,
                rows=2,
            ) as terminal:
                terminal.write(page + b"\x1b[1;2H")
                terminal.write(f"\x1b[{count}P".encode())
                self.assertEqual(terminal.snapshot().lines, [expected, "67890"])

        for count, expected in (
            (1, "134 5"),
            (2, "14  5"),
            (4, "1   5"),
        ):
            with self.subTest(margins=True, count=count), Shitty(
                columns=5,
                rows=2,
            ) as terminal:
                terminal.write(page + b"\x1b[?69h\x1b[1;4s\x1b[1;2H")
                terminal.write(f"\x1b[{count}P".encode())
                self.assertEqual(terminal.snapshot().lines, [expected, "67890"])

        with Shitty(columns=5, rows=2) as terminal:
            terminal.write(page + b"\x1b[?69h\x1b[2;4s\x1b[1;1H\x1b[P")
            self.assertEqual(terminal.snapshot().lines, ["12345", "67890"])

    def test_clear_scrollback_buffer(self):
        with Shitty(columns=5, rows=5, save_lines=1) as terminal:
            terminal.write(
                b"12345\r\n67890\r\nABCDE\r\nFGHIJ\r\nKLMNO\r\nPQRST\x1b[H"
            )
            self.assertEqual(terminal.scrollback_state()[0], 1)
            self.assertEqual(terminal.all_text()[0], "12345")
            self.assertEqual(
                terminal.snapshot().lines,
                ["67890", "ABCDE", "FGHIJ", "KLMNO", "PQRST"],
            )

            terminal.write(b"\x1b[3J")
            self.assertEqual(terminal.scrollback_state()[0], 0)
            self.assertEqual(
                terminal.snapshot().lines,
                ["67890", "ABCDE", "FGHIJ", "KLMNO", "PQRST"],
            )

    def test_erase_characters(self):
        page = b"12345\r\n67890\r\nABCDE\r\nFGHIJ\r\nKLMNO"
        for position, count, expected_line in (
            (b"\x1b[H", 0, " 2345"),
            (b"\x1b[H", 1, " 2345"),
            (b"\x1b[H", 2, "  345"),
            (b"\x1b[2;2H", 2, "6  90"),
            (b"\x1b[H", 4, "    5"),
            (b"\x1b[H", 5, "     "),
            (b"\x1b[H", 6, "     "),
        ):
            with self.subTest(position=position, count=count), Shitty(
                columns=5,
                rows=5,
            ) as terminal:
                terminal.write(page + position + f"\x1b[{count}X".encode())
                expected = ["12345", "67890", "ABCDE", "FGHIJ", "KLMNO"]
                expected[1 if position == b"\x1b[2;2H" else 0] = expected_line
                self.assertEqual(terminal.snapshot().lines, expected)

    def test_scroll_up_with_margins(self):
        page = b"12345\r\n67890\r\nABCDE\r\nFGHIJ\r\nKLMNO"
        for count, expected in (
            (1, ["12345", "6BCD0", "AGHIE", "F   J", "KLMNO"]),
            (2, ["12345", "6GHI0", "A   E", "F   J", "KLMNO"]),
            (3, ["12345", "6   0", "A   E", "F   J", "KLMNO"]),
            (4, ["12345", "6   0", "A   E", "F   J", "KLMNO"]),
        ):
            with self.subTest(count=count), Shitty(columns=5, rows=5) as terminal:
                terminal.write(page + b"\x1b[?69h\x1b[2;4s\x1b[2;4r")
                terminal.write(f"\x1b[{count}S".encode())
                self.assertEqual(terminal.snapshot().lines, expected)

    def test_scroll_up(self):
        page = b"ABC\r\nDEF\r\nGHI"
        for count, expected in (
            (1, ["DEF", "GHI", "   "]),
            (2, ["GHI", "   ", "   "]),
            (3, ["   ", "   ", "   "]),
            (4, ["   ", "   ", "   "]),
        ):
            with self.subTest(count=count), Shitty(columns=3, rows=3) as terminal:
                terminal.write(page + f"\x1b[{count}S".encode())
                self.assertEqual(terminal.snapshot().lines, expected)

    def test_scroll_down(self):
        page = b"12345\r\n67890\r\nABCDE\r\nFGHIJ\r\nKLMNO"
        for count, expected in (
            (1, ["12345", "     ", "67890", "ABCDE", "KLMNO"]),
            (3, ["12345", "     ", "     ", "     ", "KLMNO"]),
            (4, ["12345", "     ", "     ", "     ", "KLMNO"]),
        ):
            with self.subTest(margins=True, count=count), Shitty(
                columns=5,
                rows=5,
            ) as terminal:
                terminal.write(page + b"\x1b[2;4r")
                terminal.write(f"\x1b[{count}T".encode())
                self.assertEqual(terminal.snapshot().lines, expected)

        for count, expected in (
            (1, ["     ", "12345", "67890", "ABCDE", "FGHIJ"]),
            (5, ["     "] * 5),
            (6, ["     "] * 5),
        ):
            with self.subTest(margins=False, count=count), Shitty(
                columns=5,
                rows=5,
            ) as terminal:
                terminal.write(page + f"\x1b[{count}T".encode())
                self.assertEqual(terminal.snapshot().lines, expected)

    def test_unscroll_via_page_growth(self):
        with Shitty(columns=5, rows=5, save_lines=5) as terminal:
            terminal.write(
                b"AAAAA\r\nBBBBB\r\nCCCCC\r\nDDDDD\r\nEEEEE\r\nFFFFF\r\nGGGGG\r\nHHHHH"
            )
            self.assertEqual(terminal.scrollback_state()[0], 3)
            self.assertEqual(
                terminal.snapshot().lines,
                ["DDDDD", "EEEEE", "FFFFF", "GGGGG", "HHHHH"],
            )

            terminal.resize(5, 7)
            self.assertEqual(terminal.scrollback_state()[0], 1)
            self.assertEqual(
                terminal.snapshot().lines,
                ["BBBBB", "CCCCC", "DDDDD", "EEEEE", "FFFFF", "GGGGG", "HHHHH"],
            )

        with Shitty(columns=5, rows=5, save_lines=3) as terminal:
            terminal.write(
                b"AAAAA\r\nBBBBB\r\nCCCCC\r\nDDDDD\r\nEEEEE\r\nFFFFF\r\nGGGGG"
            )
            terminal.resize(5, 9)
            self.assertEqual(terminal.scrollback_state()[0], 0)
            self.assertEqual(
                terminal.snapshot().lines,
                ["AAAAA", "BBBBB", "CCCCC", "DDDDD", "EEEEE", "FFFFF", "GGGGG", "     ", "     "],
            )

    def test_sequence_cuu(self):
        for command, expected in (
            (b"\x1b[A", (1, 1)),
            (b"\x1b[0A", (1, 1)),
            (b"\x1b[1A", (1, 1)),
            (b"\x1b[5A", (1, 0)),
        ):
            with self.subTest(command=command), Shitty(columns=5, rows=5) as terminal:
                terminal.write(b"\x1b[3;2H" + command)
                snapshot = terminal.snapshot()
                self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), expected)

        for start, command, expected in (
            (b"\x1b[2;4r\x1b[3;2H", b"\x1b[A", (1, 1)),
            (b"\x1b[2;4r\x1b[3;2H", b"\x1b[5A", (1, 1)),
            (b"\x1b[3;4r\x1b[2;3H", b"\x1b[A", (2, 0)),
        ):
            with self.subTest(start=start, command=command), Shitty(
                columns=5,
                rows=5,
            ) as terminal:
                terminal.write(start + command)
                snapshot = terminal.snapshot()
                self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), expected)

    def test_move_cursor_down(self):
        for count, expected_y in ((1, 1), (5, 2)):
            with self.subTest(count=count), Shitty(columns=2, rows=3) as terminal:
                terminal.write(b"A" + f"\x1b[{count}B".encode())
                snapshot = terminal.snapshot()
                self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (1, expected_y))

    def test_move_cursor_forward(self):
        for count, expected_x in ((1, 1), (3, 2), (4, 2)):
            with self.subTest(count=count), Shitty(columns=3, rows=3) as terminal:
                terminal.write(f"\x1b[{count}C".encode())
                snapshot = terminal.snapshot()
                self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (expected_x, 0))

    def test_move_cursor_backward(self):
        for count, expected_x in ((1, 1), (5, 0)):
            with self.subTest(count=count), Shitty(columns=3, rows=3) as terminal:
                terminal.write(b"ABC" + f"\x1b[{count}D".encode())
                snapshot = terminal.snapshot()
                self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (expected_x, 0))

    def test_horizontal_position_absolute(self):
        with Shitty(columns=3, rows=3) as terminal:
            terminal.write(b"\x1b[3`")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (2, 0))

            terminal.write(b"\x1b[2`")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (1, 0))

            terminal.write(b"\x1b[5`")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (2, 0))

    def test_horizontal_position_relative(self):
        for command, expected_x in (
            (b"\x1b[a", 1),
            (b"\x1b[1a", 1),
            (b"\x1b[3a", 2),
            (b"\x1b[4a", 2),
        ):
            with self.subTest(command=command), Shitty(columns=3, rows=3) as terminal:
                terminal.write(command)
                snapshot = terminal.snapshot()
                self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (expected_x, 0))

    def test_move_cursor_to_column(self):
        with Shitty(columns=3, rows=3) as terminal:
            terminal.write(b"\x1b[3G")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (2, 0))

            terminal.write(b"\x1b[2G")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (1, 0))

            terminal.write(b"\x1b[4G")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (2, 0))

        with Shitty(columns=3, rows=3) as terminal:
            terminal.write("⚡".encode())
            snapshot = terminal.model_snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (2, 0))
            self.assertTrue(snapshot.cell(0, 0).double_width)
            self.assertTrue(snapshot.cell(1, 0).double_width_continuation)

    def test_move_cursor_to_line(self):
        with Shitty(columns=3, rows=3) as terminal:
            terminal.write(b"\x1b[3d")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 2))

            terminal.write(b"\x1b[2d")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 1))

            terminal.write(b"\x1b[4d")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 2))

    def test_move_cursor_to_begin_of_line(self):
        with Shitty(columns=3, rows=3) as terminal:
            terminal.write(b"\r\nAB")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (2, 1))

            terminal.write(b"\r")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 1))

    def test_carriage_return_honours_left_margin(self):
        setup = b"\x1b[?69h\x1b[5;10s"
        for position, expected_x in (
            (b"\x1b[1;6H", 4),
            (b"\x1b[1;5H", 4),
            (b"\x1b[1;4H", 0),
        ):
            with self.subTest(position=position), Shitty(columns=12, rows=2) as terminal:
                terminal.write(setup + position + b"\r")
                snapshot = terminal.snapshot()
                self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (expected_x, 0))

        with Shitty(columns=12, rows=2) as terminal:
            terminal.write(setup + b"\x1b[?6h\x1b[1;4H\r")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (4, 0))

    def test_nel_indexes_and_returns_to_margin(self):
        page = b"12345\r\n67890\r\nABCDE\r\nFGHIJ\r\nKLMNO"
        with Shitty(columns=5, rows=5) as terminal:
            terminal.write(page + b"\x1b[3;5H\x1bE")
            snapshot = terminal.snapshot()
            self.assertEqual(
                snapshot.lines,
                ["12345", "67890", "ABCDE", "FGHIJ", "KLMNO"],
            )
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 3))

        with Shitty(columns=3, rows=3) as terminal:
            terminal.write(b"111\r\n222\r\n333\x1b[3;3H\x1bE")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, ["222", "333", "   "])
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 2))

        margins = b"\x1b[2;4r\x1b[?69h\x1b[2;4s"
        with Shitty(columns=5, rows=5) as terminal:
            terminal.write(page + margins + b"\x1b[4;5H\x1bE")
            snapshot = terminal.snapshot()
            self.assertEqual(
                snapshot.lines,
                ["12345", "67890", "ABCDE", "FGHIJ", "KLMNO"],
            )
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (1, 3))

        with Shitty(columns=5, rows=5) as terminal:
            terminal.write(page + margins + b"\x1b[4;3H\x1bE")
            snapshot = terminal.snapshot()
            self.assertEqual(
                snapshot.lines,
                ["12345", "6BCD0", "AGHIE", "F   J", "KLMNO"],
            )
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (1, 3))

    def test_sd_respects_left_right_margin(self):
        with Shitty(columns=5, rows=7) as terminal:
            terminal.write(
                b"abcde\r\nfghij\r\nklmno\r\npqrst\r\nuvwxy"
                b"\x1b[?69h\x1b[2;4s\x1b[2;3H\x1b[2T"
            )
            self.assertEqual(
                terminal.snapshot().lines,
                ["a   e", "f   j", "kbcdo", "pghit", "ulmny", " qrs ", " vwx "],
            )

    def test_il_over_region_clears_the_band(self):
        with Shitty(columns=5, rows=5) as terminal:
            terminal.write(
                b"abcde\r\nfGHIj\r\nkLMNo\r\npQRSt\r\nuvwxy"
                b"\x1b[?69h\x1b[2;4s\x1b[2;4r\x1b[2;3H\x1b[99L"
            )
            self.assertEqual(
                terminal.snapshot().lines,
                ["abcde", "f   j", "k   o", "p   t", "uvwxy"],
            )

    def test_autowrap_within_left_right_margin(self):
        with Shitty(columns=6, rows=2) as terminal:
            terminal.write(b"\x1b[?69h\x1b[2;4s\x1b[1;2Hxyzw")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, [" xyz  ", " w    "])

        with Shitty(columns=6, rows=2) as terminal:
            terminal.write(b"\x1b[?69h\x1b[2;4s\x1b[?7l\x1b[1;2Hxyzw")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, [" xyw  ", "      "])

    def test_decbi_back_index(self):
        with Shitty(columns=6, rows=6) as terminal:
            terminal.write(b"\x1b[6;5H\x1b6")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (3, 5))

        with Shitty(columns=6, rows=6) as terminal:
            terminal.write(b"\x1b[2;1H\x1b6")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 1))

        with Shitty(columns=12, rows=6) as terminal:
            terminal.write(b"\x1b[?69h\x1b[3;5s\x1b[1;2H\x1b6")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 0))

        with Shitty(columns=6, rows=7) as terminal:
            terminal.write(
                b"\x1b[3;2Habcde\x1b[4;2Hfghij\x1b[5;2Hklmno"
                b"\x1b[6;2Hpqrst\x1b[7;2Huvwxy"
                b"\x1b[?69h\x1b[3;5s\x1b[4;6r\x1b[5;3H\x1b6"
            )
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines[2], " abcde")
            self.assertEqual(snapshot.lines[3], " f ghj")
            self.assertEqual(snapshot.lines[4], " k lmo")
            self.assertEqual(snapshot.lines[5], " p qrt")
            self.assertEqual(snapshot.lines[6], " uvwxy")
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (2, 4))

    def test_cnl_cpl_clamp_to_scroll_region_and_left_margin(self):
        setup = b"\x1b[2;4r\x1b[?69h\x1b[5;10s"
        for position, command, expected in (
            (b"\x1b[3;7H", b"\x1b[99E", (4, 3)),
            (b"\x1b[3;7H", b"\x1b[99F", (4, 1)),
        ):
            with self.subTest(command=command), Shitty(columns=12, rows=6) as terminal:
                terminal.write(setup + position + command)
                snapshot = terminal.snapshot()
                self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), expected)

        with Shitty(columns=12, rows=6) as terminal:
            terminal.write(b"\x1b[3;4r\x1b[?69h\x1b[5;10s\x1b[5;7H\x1b[99E")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (4, 5))

        with Shitty(columns=12, rows=6) as terminal:
            terminal.write(b"\x1b[3;7H\x1b[99E")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 5))

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
