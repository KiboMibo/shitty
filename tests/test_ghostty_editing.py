# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import unittest

from harness import Shitty, put_rows


class GhosttyEditingTest(unittest.TestCase):
    def test_invalid_equal_vertical_margins_keep_the_previous_region(self):
        with Shitty(columns=5, rows=5) as terminal:
            terminal.write(put_rows(b"A", b"B", b"C", b"D", b"E"))
            terminal.write(
                b"\x1b[2;4r"
                b"\x1b[3;3r"
                b"\x1b[2;1H\x1b[S"
            )
            self.assertEqual(
                terminal.snapshot().lines,
                ["A    ", "C    ", "D    ", "     ", "E    "],
            )
            self.assertEqual(terminal.last_update_rows(), (1, 2, 3))

    def test_invalid_equal_horizontal_margins_keep_the_previous_region(self):
        with Shitty(columns=5, rows=4) as terminal:
            terminal.write(put_rows(b"ABC", b"DEF", b"GHI"))
            terminal.write(
                b"\x1b[?69h"
                b"\x1b[2;4s"
                b"\x1b[3;3s"
                b"\x1b[2;2H\x1b[L"
            )
            self.assertEqual(
                terminal.snapshot().lines,
                ["ABC  ", "D    ", "GEF  ", " HI  "],
            )
            self.assertEqual(terminal.last_update_rows(), (1, 2, 3))

    def test_insert_line_resets_pending_wrap_and_wrap_metadata(self):
        with Shitty(columns=5, rows=5) as terminal:
            terminal.write(b"ABCDE")
            self.assertTrue(terminal.cursor_pending_wrap())

            terminal.write(b"\x1b[L")
            snapshot = terminal.model_snapshot()
            self.assertFalse(terminal.cursor_pending_wrap())
            self.assertEqual(
                snapshot.lines,
                ["     ", "ABCDE", "     ", "     ", "     "],
            )
            self.assertFalse(snapshot.cell(4, 1).wrapped)
            self.assertEqual(terminal.last_update_rows(), (0, 1, 2, 3, 4))

            terminal.write(b"B")
            self.assertEqual(terminal.snapshot().lines[0], "B    ")

    def test_insert_line_moves_grapheme_and_hyperlink_metadata(self):
        family = "👨‍👩‍👧"
        with Shitty(columns=10, rows=4) as terminal:
            terminal.write(
                b"ABC"
                b"\x1b[2;1H"
                b"\x1b]8;;https://example.test\x1b\\"
                + family.encode()
                + b"\x1b]8;;\x1b\\"
                b"\x1b[3;1HGHI"
                b"\x1b[4;1HJKL"
            )
            terminal.write(b"\x1b[2;5H\x1b[L")
            snapshot = terminal.model_snapshot()

            self.assertEqual(
                snapshot.lines,
                ["ABC       ", "          ", "👨         ", "GHI       "],
            )
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 1))
            self.assertEqual(
                snapshot.cell(0, 2).grapheme,
                tuple(map(ord, family)),
            )
            self.assertTrue(snapshot.cell(0, 2).double_width)
            self.assertNotEqual(snapshot.cell(0, 2).hyperlink, 0)
            self.assertEqual(
                terminal.hyperlink(0, 2),
                "https://example.test",
            )
            self.assertEqual(terminal.last_update_rows(), (1, 2, 3))

    def test_insert_line_obeys_both_margin_pairs(self):
        with Shitty(columns=10, rows=5) as terminal:
            terminal.write(put_rows(b"ABC123", b"DEF456", b"GHI789"))
            terminal.write(
                b"\x1b[?69h"
                b"\x1b[2;4s"
                b"\x1b[2;2H\x1b[L"
            )
            self.assertEqual(
                terminal.snapshot().lines,
                [
                    "ABC123    ",
                    "D   56    ",
                    "GEF489    ",
                    " HI7      ",
                    "          ",
                ],
            )
            self.assertEqual(terminal.last_update_rows(), (1, 2, 3, 4))

    def test_scroll_preserves_pending_wrap_until_the_next_print(self):
        cases = (
            (
                b"S",
                ["    B", "    C", "     ", "X    ", "     "],
            ),
            (
                b"T",
                ["     ", "    A", "    B", "X   C", "     "],
            ),
        )
        for operation, expected in cases:
            with self.subTest(operation=operation), Shitty(
                columns=5,
                rows=5,
            ) as terminal:
                terminal.write(
                    b"\x1b[1;5HA"
                    b"\x1b[2;5HB"
                    b"\x1b[3;5HC"
                )
                self.assertTrue(terminal.cursor_pending_wrap())

                terminal.write(b"\x1b[" + operation)
                self.assertTrue(terminal.cursor_pending_wrap())
                self.assertEqual(
                    terminal.last_update_rows(),
                    (0, 1, 2, 3, 4),
                )

                terminal.write(b"X")
                self.assertEqual(terminal.snapshot().lines, expected)

    def test_partial_width_scroll_moves_only_matching_hyperlinks(self):
        cases = (
            (
                b"S",
                ["AEF423    ", "DHI756    ", "G   89    ", "          "],
                (
                    (False, True, True, True, False, False),
                    (True, False, False, False, True, True),
                ),
            ),
            (
                b"T",
                ["A   23    ", "DBC156    ", "GEF489    ", " HI7      "],
                (
                    (False, False, False, False, False, False),
                    (True, False, False, False, True, True),
                    (False, True, True, True, False, False),
                ),
            ),
        )
        for operation, expected, hyperlinks in cases:
            with self.subTest(operation=operation), Shitty(
                columns=10,
                rows=4,
            ) as terminal:
                terminal.write(put_rows(b"ABC123", b"DEF456", b"GHI789"))
                terminal.write(
                    b"\x1b[2;1H"
                    b"\x1b]8;;https://example.com\x1b\\"
                    b"DEF456"
                    b"\x1b]8;;\x1b\\"
                    b"\x1b[?69h\x1b[2;4s"
                    b"\x1b[" + operation
                )
                snapshot = terminal.model_snapshot()
                self.assertEqual(snapshot.lines, expected)
                self.assertEqual(terminal.last_update_rows(), (0, 1, 2, 3))
                for row, expected_row in enumerate(hyperlinks):
                    self.assertEqual(
                        tuple(
                            bool(snapshot.cell(column, row).hyperlink)
                            for column in range(6)
                        ),
                        expected_row,
                    )
                    for column, linked in enumerate(expected_row):
                        self.assertEqual(
                            terminal.hyperlink(column, row),
                            "https://example.com" if linked else "",
                        )


if __name__ == "__main__":
    unittest.main()
