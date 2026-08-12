# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import unittest

from harness import Shitty


class ChangeRectangleAttributesTest(unittest.TestCase):
    def test_kitty_full_sgr_transaction(self):
        with Shitty(columns=5, rows=5, save_lines=0) as terminal:
            terminal.write(b"\x1b[$r")
            terminal.write(
                b"\x1b[;;;;4:3;38:5:10;48:2:1:2:3;1$r"
            )

            snapshot = terminal.model_snapshot()
            for cell in snapshot.cells:
                self.assertTrue(cell.bold)
                self.assertFalse(cell.italic)
                self.assertEqual(cell.underline_style, 3)
                self.assertEqual(cell.foreground_index, 10)
                self.assertEqual(cell.background, (1, 2, 3))

            terminal.write(b"\x1b[1;2;2;3;22;39$r")
            snapshot = terminal.model_snapshot()
            for row in range(snapshot.rows):
                for column in range(snapshot.columns):
                    cell = snapshot.cell(column, row)
                    in_stream_extent = (
                        row == 0 and column >= 1
                    ) or (
                        row == 1 and column <= 2
                    )
                    self.assertEqual(cell.bold, not in_stream_extent)
                    self.assertEqual(
                        cell.foreground_index,
                        -2 if in_stream_extent else 10,
                    )

            terminal.write(
                b"\x1b[2*x"
                b"\x1b[3;2;4;3;34$r"
                b"\x1b[*x"
            )
            snapshot = terminal.model_snapshot()
            for row in range(snapshot.rows):
                for column in range(snapshot.columns):
                    changed = 2 <= row <= 3 and 1 <= column <= 2
                    expected = 4 if changed else (
                        -2 if (
                            row == 0 and column >= 1
                        ) or (
                            row == 1 and column <= 2
                        ) else 10
                    )
                    self.assertEqual(
                        snapshot.cell(column, row).foreground_index,
                        expected,
                    )


if __name__ == "__main__":
    unittest.main()
