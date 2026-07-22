import unittest

from harness import Zutty


class EditingTest(unittest.TestCase):
    def test_rectangle_origin_tracks_origin_mode_and_both_margin_pairs(self):
        with Zutty(columns=8, rows=4) as terminal:
            self.assertEqual(terminal.rectangle_origin(), (0, 0, 4, 8))
            terminal.write(b"\x1b[2;3r\x1b[?69h\x1b[3;6s\x1b[?6h")
            self.assertEqual(terminal.rectangle_origin(), (1, 2, 3, 6))

    def test_ecma48_spa_epa_mark_only_guarded_characters(self):
        for spa, epa in ((b"\x1bV", b"\x1bW"), (b"\x96", b"\x97")):
            with self.subTest(spa=spa):
                with Zutty(columns=8, rows=2) as terminal:
                    terminal.write(b"A" + spa + b"BC" + epa + b"D")
                    snapshot = terminal.snapshot()
                    self.assertFalse(snapshot.cell(0, 0).protected)
                    self.assertTrue(snapshot.cell(1, 0).protected)
                    self.assertTrue(snapshot.cell(2, 0).protected)
                    self.assertFalse(snapshot.cell(3, 0).protected)

    def test_ecma48_erm_controls_normal_erasure(self):
        for erase in (b"\x1b[2J", b"\x1b[2K", b"\x1b[8X"):
            with self.subTest(erase=erase), Zutty(columns=8, rows=2) as terminal:
                terminal.write(b"A\x1bVBC\x1bWDE\x1b[H" + erase)
                self.assertEqual(terminal.snapshot().lines[0], " BC     ")

        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"A\x1bVBC\x1bWDE\x1b[H")
            terminal.write(b"\x1b[6h\x1b[2J\x1b[H")
            self.assertEqual(terminal.snapshot().lines[0], "        ")

            terminal.write(b"A\x1bVBC\x1bWDE\x1b[6l\x1b[2J")
            self.assertEqual(terminal.snapshot().lines[0], " BC     ")

    def test_ecma48_erm_does_not_treat_decsca_as_iso_protection(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(
                b"\x1bVISO\x1bW"
                b"\x1b[1\"qDEC\x1b[0\"q"
                b"\x1b[H\x1b[2J"
            )
            self.assertEqual(terminal.snapshot().lines[0], "ISO     ")

    def test_selective_erase_preserves_decsca_protected_cells(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(
                b"ab\x1b[1\"qCD\x1b[0\"qefgh"
                b"\x1b[1;4H\x1b[?2K"
            )
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines[0], "  CD    ")
            self.assertTrue(snapshot.cell(2, 0).protected)
            self.assertFalse(snapshot.cell(0, 0).protected)

    def test_dec_selective_erase_ignores_iso_guarded_areas(self):
        for erase in (b"\x1b[?2J", b"\x1b[?2K", b"\x1b[1;1;1;6${"):
            with self.subTest(erase=erase), Zutty(columns=6, rows=2) as terminal:
                terminal.write(
                    b"A\x1bVB\x1bWC"
                    b"\x1b[1\"qD\x1b[0\"qE"
                    b"\x1b[H" + erase
                )
                self.assertEqual(terminal.snapshot().lines[0], "   D  ")

    def test_selective_display_erase_obeys_cursor_and_defaults(self):
        with Zutty(columns=5, rows=2) as terminal:
            terminal.write(
                b"A\x1b[1\"qB\x1b[0\"qCDE\r\n"
                b"F\x1b[1\"qG\x1b[0\"qHIJ"
                b"\x1b[1;3H\x1b[?J"
            )
            self.assertEqual(terminal.snapshot().lines, ["AB   ", " G   "])

    def test_rectangular_fill_erase_and_overlap_safe_copy(self):
        with Zutty(columns=6, rows=3) as terminal:
            terminal.write(b"abcdef\x1b[2;1Hghijkl\x1b[3;1Hmnopqr")
            terminal.write(b"\x1b[88;1;2;2;3$x")
            self.assertEqual(
                terminal.snapshot().lines, ["aXXdef", "gXXjkl", "mnopqr"]
            )
            terminal.write(b"\x1b[2;2;2;4$z")
            self.assertEqual(terminal.snapshot().lines[1], "g   kl")
            terminal.write(b"\x1b[1;1;2;3;1;2;3;1$v")
            self.assertEqual(terminal.snapshot().lines[1], "g aXXl")

    def test_rectangular_attribute_change_and_reverse(self):
        with Zutty(columns=6, rows=2) as terminal:
            terminal.write(b"abcdef\x1b[1;2;1;4;1;4;7$r")
            snapshot = terminal.snapshot()
            self.assertTrue(snapshot.cell(1, 0).bold)
            self.assertTrue(snapshot.cell(1, 0).underline)
            self.assertTrue(snapshot.cell(1, 0).inverse)
            terminal.write(b"\x1b[1;2;1;4;1;4;7$t")
            snapshot = terminal.snapshot()
            self.assertFalse(snapshot.cell(1, 0).bold)
            self.assertFalse(snapshot.cell(1, 0).underline)
            self.assertFalse(snapshot.cell(1, 0).inverse)

    def test_rectangular_checksum_reply_is_stable(self):
        with Zutty(columns=4, rows=2) as terminal:
            terminal.write(b"AB\x1b[7;1;1;1;1;2*y")
            self.assertEqual(terminal.read_input(), b"\x1bP7!~FF7D\x1b\\")
    def test_insert_and_delete_characters(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"abcdef\x1b[1;3H\x1b[2@XY")
            self.assertEqual(terminal.snapshot().lines[0], "abXYcdef")
            terminal.write(b"\x1b[1;3H\x1b[3P")
            self.assertEqual(terminal.snapshot().lines[0], "abdef   ")

    def test_character_editing_never_leaves_split_wide_cells(self):
        for operation in (b"\x1b[@", b"\x1b[P", b"\x1b[X"):
            with self.subTest(operation=operation):
                with Zutty(columns=8, rows=2) as terminal:
                    terminal.write("A界BC".encode())
                    terminal.write(b"\x1b[1;3H" + operation)
                    snapshot = terminal.snapshot()
                    for column, cell in enumerate(snapshot.cells[:8]):
                        if cell.double_width:
                            self.assertLess(column + 1, 8)
                            self.assertTrue(
                                snapshot.cell(column + 1, 0).double_width_continuation
                            )
                        if cell.double_width_continuation:
                            self.assertGreater(column, 0)
                            self.assertTrue(
                                snapshot.cell(column - 1, 0).double_width
                            )

    def test_erase_characters_and_line(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"abcdefgh\x1b[1;3H\x1b[3X")
            self.assertEqual(terminal.snapshot().lines[0], "ab   fgh")
            terminal.write(b"\x1b[1;5H\x1b[1K")
            self.assertEqual(terminal.snapshot().lines[0], "     fgh")

    def test_erased_cells_keep_colors_but_clear_character_attributes(self):
        for erase in (b"\x1b[2J", b"\x1b[2K", b"\x1b[8X", b"\x1b[4@"):
            with self.subTest(erase=erase), Zutty(columns=8, rows=2) as terminal:
                terminal.write(
                    b"12345678\x1b[H"
                    b"\x1b[1;2;3;4;5;7;8;9;53;31;42m"
                    b"\x1b[1\"q\x1b]8;;https://example.com\x1b\\"
                    + erase
                )
                cell = terminal.snapshot().cell(0, 0)
                self.assertEqual(cell.char, " ")
                self.assertEqual(cell.foreground, (205, 0, 0))
                self.assertEqual(cell.background, (0, 205, 0))
                self.assertFalse(cell.bold)
                self.assertFalse(cell.faint)
                self.assertFalse(cell.italic)
                self.assertFalse(cell.underline)
                self.assertFalse(cell.blink)
                self.assertFalse(cell.inverse)
                self.assertFalse(cell.conceal)
                self.assertFalse(cell.strike)
                self.assertFalse(cell.overline)
                self.assertFalse(cell.protected)
                self.assertEqual(cell.hyperlink, 0)

    def test_erase_characters_outside_horizontal_margins_stays_on_screen(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(
                b"ABCDEFGH"
                b"\x1b[?69h\x1b[1;4s"
                b"\x1b[1;8H\x1b[99X"
            )
            self.assertEqual(terminal.snapshot().lines[0], "ABCDEFG ")

    def test_erase_characters_ignores_horizontal_margins(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(
                b"ABCDEFGH"
                b"\x1b[?69h\x1b[2;4s"
                b"\x1b[1;3H\x1b[4X"
            )
            self.assertEqual(terminal.snapshot().lines[0], "AB    GH")

    def test_insert_and_delete_lines(self):
        with Zutty(columns=5, rows=4) as terminal:
            terminal.write(b"one\r\ntwo\r\nthree\r\nfour")
            terminal.write(b"\x1b[2;1H\x1b[L")
            self.assertEqual(
                terminal.snapshot().lines,
                ["one  ", "     ", "two  ", "three"],
            )
            terminal.write(b"\x1b[2;1H\x1b[M")
            self.assertEqual(
                terminal.snapshot().lines,
                ["one  ", "two  ", "three", "     "],
            )

    def test_insert_lines_clamps_count_to_remaining_region(self):
        with Zutty(columns=5, rows=4) as terminal:
            terminal.write(b"one\r\ntwo\r\nthree\r\nfour")
            terminal.write(b"\x1b[1;1H\x1b[999L")
            self.assertEqual(terminal.snapshot().lines, ["     "] * 4)

    def test_insert_and_delete_lines_move_cursor_to_line_home(self):
        for operation in (b"\x1b[L", b"\x1b[M"):
            with self.subTest(operation=operation):
                with Zutty(columns=8, rows=4) as terminal:
                    terminal.write(b"one\r\ntwo\r\nthree\x1b[2;5H")
                    terminal.write(operation + b"X")
                    self.assertEqual(terminal.snapshot().cell(0, 1).char, "X")

    def test_erase_display(self):
        with Zutty(columns=5, rows=3) as terminal:
            terminal.write(b"one\r\ntwo\r\nthree\x1b[2;2H\x1b[J")
            self.assertEqual(terminal.snapshot().lines, ["one  ", "t    ", "     "])

    def test_insert_mode(self):
        with Zutty(columns=6, rows=2) as terminal:
            terminal.write(b"abcd\x1b[1;2H\x1b[4hX\x1b[4lY")
            self.assertEqual(terminal.snapshot().lines[0], "aXYcd ")

    def test_scroll_up_and_down(self):
        with Zutty(columns=5, rows=4) as terminal:
            terminal.write(b"one\r\ntwo\r\nthree\r\nfour\x1b[2S")
            self.assertEqual(
                terminal.snapshot().lines,
                ["three", "four ", "     ", "     "],
            )
        with Zutty(columns=5, rows=4) as terminal:
            terminal.write(b"one\r\ntwo\r\nthree\r\nfour\x1b[2T")
            self.assertEqual(
                terminal.snapshot().lines,
                ["     ", "     ", "one  ", "two  "],
            )

    def test_large_scroll_count_is_clamped_to_region(self):
        with Zutty(columns=5, rows=3) as terminal:
            terminal.write(b"one\r\ntwo\r\nthree\x1b[999S")
            self.assertEqual(terminal.snapshot().lines, ["     "] * 3)
            terminal.write(b"one\r\ntwo\r\nthree\x1b[999T")
            self.assertEqual(terminal.snapshot().lines, ["     "] * 3)

    def test_scroll_left_and_right(self):
        with Zutty(columns=4, rows=2) as terminal:
            terminal.write(b"abcd\x1b[2;1Hefgh\x1b[2 @")
            self.assertEqual(terminal.snapshot().lines, ["cd  ", "gh  "])
        with Zutty(columns=4, rows=2) as terminal:
            terminal.write(b"abcd\x1b[2;1Hefgh\x1b[2 A")
            self.assertEqual(terminal.snapshot().lines, ["  ab", "  ef"])

    def test_repeat_preceding_graphic_character(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"A\x1b[5b")
            self.assertEqual(terminal.snapshot().lines[0], "AAAAAA  ")

    def test_forward_and_backward_tabulation(self):
        with Zutty(columns=20, rows=2) as terminal:
            terminal.write(b"\x1b[2IX\x1b[20G\x1b[2ZY")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(16, 0).char, "X")
            self.assertEqual(snapshot.cell(8, 0).char, "Y")

    def test_insert_and_delete_columns(self):
        with Zutty(columns=6, rows=2) as terminal:
            terminal.write(b"abcdef\x1b[2;1Hghijkl\x1b[1;3H\x1b[2'}")
            self.assertEqual(terminal.snapshot().lines, ["ab  cd", "gh  ij"])
            terminal.write(b"\x1b[2'~")
            self.assertEqual(terminal.snapshot().lines, ["abcd  ", "ghij  "])

    def test_erase_saved_lines_discards_scrollback(self):
        with Zutty(columns=8, rows=3, save_lines=8) as terminal:
            terminal.write(b"one\r\ntwo\r\nthree\r\nfour\x1b[3J")
            terminal.page_up()
            self.assertEqual(terminal.snapshot().view_offset, 0)

    def test_erase_saved_lines_preserves_live_screen(self):
        with Zutty(columns=8, rows=3, save_lines=8) as terminal:
            terminal.write(b"one\r\ntwo\r\nthree\r\nfour")
            before = terminal.snapshot().lines
            terminal.write(b"\x1b[3J")

            self.assertEqual(terminal.snapshot().lines, before)


if __name__ == "__main__":
    unittest.main()
