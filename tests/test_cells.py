import unittest

from harness import Zutty


class CellStateTest(unittest.TestCase):
    def test_sgr_attributes_and_truecolor_are_observable(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[1;3;4;7;38;2;1;2;3;48;2;4;5;6mX")
            cell = terminal.snapshot().cell(0, 0)

            self.assertEqual(cell.char, "X")
            self.assertTrue(cell.bold)
            self.assertTrue(cell.italic)
            self.assertTrue(cell.underline)
            # Zutty materializes inverse video by swapping the stored colors.
            self.assertEqual(cell.foreground, (4, 5, 6))
            self.assertEqual(cell.background, (1, 2, 3))

    def test_sgr_resets_individual_attributes(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[1;3;4;7mA\x1b[22;23;24;27mB")
            snapshot = terminal.snapshot()

            first = snapshot.cell(0, 0)
            second = snapshot.cell(1, 0)
            self.assertTrue(first.bold and first.italic)
            self.assertTrue(first.underline)
            self.assertFalse(second.bold or second.italic)
            self.assertFalse(second.underline)

    def test_wide_character_occupies_two_cells(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write("界".encode())
            snapshot = terminal.snapshot()

            self.assertEqual(snapshot.cell(0, 0).char, "界")
            self.assertTrue(snapshot.cell(0, 0).double_width)
            self.assertTrue(snapshot.cell(1, 0).double_width_continuation)

    def test_autowrap_is_recorded_on_last_cell(self):
        with Zutty(columns=4, rows=2) as terminal:
            terminal.write(b"abcde")
            snapshot = terminal.snapshot()

            self.assertEqual(snapshot.lines, ["abcd", "e   "])
            self.assertTrue(snapshot.cell(3, 0).wrapped)


if __name__ == "__main__":
    unittest.main()
