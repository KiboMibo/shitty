"""Terminals behind one window."""

import time
import unittest

from harness import Shitty


class TabTest(unittest.TestCase):
    def test_a_fresh_window_has_one_session_and_shows_it(self):
        with Shitty(columns=8, rows=2) as terminal:
            self.assertEqual(terminal.session_state(), (1, 0))

    def test_feeding_a_background_session_stays_off_the_shown_screen(self):
        # WRITE always feeds the first session's terminal; once another
        # session is active, those bytes belong to a background screen and
        # must neither show through nor get lost.
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"AB")
            terminal.new_session()
            terminal.write(b"XY")
            self.assertNotIn("XY", terminal.screen_text())
            terminal.close_session(1)
            terminal.present()
            self.assertIn("ABXY", terminal.screen_text())

    def test_typing_reaches_only_the_active_sessions_shell(self):
        # Keystrokes route through the session set to the active terminal,
        # which writes its own pty - never the first session's shell.
        with Shitty(columns=8, rows=2) as terminal:
            terminal.new_session()
            terminal.frontend_text_event("x")
            self.assertEqual(terminal.read_input(), b"")
            terminal.close_session(1)
            terminal.frontend_text_event("y")
            self.assertEqual(terminal.read_input(), b"y")

    def test_background_sessions_track_the_window_resize(self):
        # A resize lands on every session, background ones included; a
        # terminal that only resized on activation would come back with
        # the old grid.
        with Shitty(columns=8, rows=2) as terminal:
            terminal.new_session()
            terminal.resize(6, 2)
            terminal.close_session(1)
            terminal.present()
            rows = terminal.screen_text().splitlines()
            self.assertTrue(rows)
            for row in rows:
                self.assertLessEqual(len(row), 6)

    def test_closing_a_blinking_background_session_is_clean(self):
        # The first session arms its blink deadline, goes to the
        # background and is closed with the timer still pending; later
        # blink ticks must find a coherent world.
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[5mAB\x1b[0m")
            terminal.new_session()
            terminal.close_session(0)
            self.assertEqual(terminal.session_state(), (1, 0))
            terminal.blink_tick()
            self.assertEqual(terminal.session_state(), (1, 0))

    def test_a_closed_sessions_arena_is_reaped_and_reused(self):
        # A session opened and closed leaves a grave; after a presented
        # frame the reaper drops its arena, and the window keeps opening
        # fresh sessions over the reclaimed ground.
        with Shitty(columns=8, rows=2) as terminal:
            for _ in range(3):
                terminal.new_session()
                terminal.close_session(1)
                terminal.present()
                # The reaper polls its graves; give the deadline a chance
                # to pass before the next command turns the loop.
                time.sleep(0.05)
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
