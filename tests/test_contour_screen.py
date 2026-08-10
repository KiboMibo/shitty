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


class ContourScreenTest(unittest.TestCase):
    def test_upstream_inventory_has_all_12_cases(self):
        self.assertEqual(len(UPSTREAM_CASES), 12)
        self.assertEqual(len(set(UPSTREAM_CASES)), 12)

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


if __name__ == "__main__":
    unittest.main()
