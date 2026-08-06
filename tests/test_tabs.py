"""Terminals behind one window."""

import unittest

from harness import Shitty


class TabTest(unittest.TestCase):
    def test_a_fresh_window_has_one_session_and_shows_it(self):
        with Shitty(columns=8, rows=2) as terminal:
            self.assertEqual(terminal.session_state(), (1, 0))

    def test_opening_a_session_shows_it_and_keeps_the_first(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.new_session()
            self.assertEqual(terminal.session_state(), (2, 1))
            terminal.new_session()
            self.assertEqual(terminal.session_state(), (3, 2))

    def test_closing_a_session_activates_a_neighbour(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.new_session()
            terminal.new_session()
            terminal.close_session(2)
            count, active = terminal.session_state()
            self.assertEqual(count, 2)
            self.assertLess(active, count)

    def test_closing_back_to_one_leaves_a_session_showing(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.new_session()
            terminal.close_session(1)
            self.assertEqual(terminal.session_state(), (1, 0))


if __name__ == "__main__":
    unittest.main()
