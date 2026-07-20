import unittest

from harness import Zutty


def populate(terminal):
    terminal.write(b"1\r\n2\r\n3\r\n4\r\n5\r\n6")


class ResizeViewportTest(unittest.TestCase):
    def test_width_resize_preserves_view_offset_and_top_row(self):
        with Zutty(columns=5, rows=3, save_lines=6) as terminal:
            populate(terminal)
            terminal.wheel_up(2)
            before = terminal.snapshot()
            terminal.resize(8, 3)
            after = terminal.snapshot()
            self.assertEqual(before.view_offset, 2)
            self.assertEqual(after.view_offset, 2)
            self.assertEqual(after.lines, ["2       ", "3       ", "4       "])

    def test_height_shrink_keeps_scrolled_view_anchored(self):
        with Zutty(columns=5, rows=3, save_lines=6) as terminal:
            populate(terminal)
            terminal.wheel_up(2)
            terminal.resize(5, 2)
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.view_offset, 3)
            self.assertEqual(snapshot.lines, ["2    ", "3    "])

    def test_height_grow_keeps_same_top_row_when_possible(self):
        with Zutty(columns=5, rows=3, save_lines=6) as terminal:
            populate(terminal)
            terminal.wheel_up(2)
            terminal.resize(5, 2)
            terminal.resize(5, 4)
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.view_offset, 1)
            self.assertEqual(snapshot.lines, ["2    ", "3    ", "4    ", "5    "])

    def test_selection_tracks_same_history_text_through_resize(self):
        with Zutty(columns=5, rows=3, save_lines=6) as terminal:
            populate(terminal)
            terminal.wheel_up(2)
            terminal.select_start(0, 0)
            terminal.select_update(1, 0)
            terminal.resize(7, 2)
            self.assertEqual(terminal.select_finish(), b"2")

    def test_output_after_scrolled_resize_does_not_jump_to_bottom(self):
        with Zutty(columns=5, rows=3, save_lines=6) as terminal:
            populate(terminal)
            terminal.wheel_up(2)
            terminal.resize(7, 3)
            terminal.write(b"\r\n7")
            snapshot = terminal.snapshot()
            self.assertGreater(snapshot.view_offset, 0)
            self.assertEqual(snapshot.lines[0].rstrip(), "2")


if __name__ == "__main__":
    unittest.main()
