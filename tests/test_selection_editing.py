import unittest

from harness import Shitty, put_rows


class SelectionEditingTest(unittest.TestCase):
    def test_erase_overlapping_linear_selection_clears_it(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"abcdefgh")
            terminal.select_start(2, 0)
            terminal.select_update(6, 0)
            terminal.write(b"\x1b[1;4H\x1b[2X")
            self.assertEqual(terminal.snapshot().selection, (-1, -1, -1, -1))
            self.assertEqual(terminal.select_finish(), b"")

    def test_disjoint_erase_keeps_linear_selection(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"abcdefgh")
            terminal.select_start(1, 0)
            terminal.select_update(3, 0)
            terminal.write(b"\x1b[1;6H\x1b[2X")
            self.assertEqual(terminal.select_finish(), b"bc")

    def test_insert_or_delete_before_selection_invalidates_moved_text(self):
        for edit in (b"\x1b[@", b"\x1b[P"):
            with self.subTest(edit=edit):
                with Shitty(columns=8, rows=2) as terminal:
                    terminal.write(b"abcdefgh")
                    terminal.select_start(3, 0)
                    terminal.select_update(6, 0)
                    terminal.write(b"\x1b[1;2H" + edit)
                    self.assertEqual(terminal.select_finish(), b"")

    def test_edit_after_selection_keeps_unchanged_text(self):
        for edit in (b"\x1b[@", b"\x1b[P", b"\x1b[2X"):
            with self.subTest(edit=edit):
                with Shitty(columns=8, rows=2) as terminal:
                    terminal.write(b"abcdefgh")
                    terminal.select_start(0, 0)
                    terminal.select_update(2, 0)
                    terminal.write(b"\x1b[1;6H" + edit)
                    self.assertEqual(terminal.select_finish(), b"ab")

    def test_rectangular_selection_ignores_edits_outside_its_columns(self):
        with Shitty(columns=8, rows=3) as terminal:
            terminal.write(put_rows(b"abcdefgh", b"ijklmnop", b"qrstuvwx"))
            terminal.select_start(1, 0)
            terminal.select_rectangular()
            terminal.select_update(4, 2)
            terminal.write(b"\x1b[2;6H\x1b[2X")
            self.assertEqual(terminal.select_finish(), b"bcd\njkl\nrst")

    def test_rectangular_selection_is_cleared_by_overlapping_edit(self):
        with Shitty(columns=8, rows=3) as terminal:
            terminal.write(put_rows(b"abcdefgh", b"ijklmnop", b"qrstuvwx"))
            terminal.select_start(1, 0)
            terminal.select_rectangular()
            terminal.select_update(4, 2)
            terminal.write(b"\x1b[2;3H\x1b[X")
            self.assertEqual(terminal.select_finish(), b"")

    def test_noop_selective_erase_keeps_protected_selection(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[1\"qABC\x1b[0\"q")
            terminal.select_start(0, 0)
            terminal.select_update(3, 0)
            terminal.write(b"\x1b[1;1H\x1b[?2K")
            self.assertEqual(terminal.select_finish(), b"ABC")


if __name__ == "__main__":
    unittest.main()
