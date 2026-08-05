"""Terminals behind one window."""

import unittest

from harness import Shitty


class TabTest(unittest.TestCase):
    def test_a_fresh_window_has_one_session_and_shows_it(self):
        with Shitty(columns=8, rows=2) as terminal:
            self.assertEqual(terminal.session_state(), (1, 0))


if __name__ == "__main__":
    unittest.main()
