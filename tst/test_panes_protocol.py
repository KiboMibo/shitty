# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

"""Panes addressed through the test protocol (K2).

The protocol half of a split: how many panes a tab shows, which index
names which one, what a pane's own pty was born with, and what happens to
a command that names a pane nobody has. Splitting itself is a session-set
feature covered by unit tests; what is pinned here is that a test can
reach one pane out of several without reaching its neighbour, because
every pane test written after this one leans on exactly that.

The terminal needs `-panes` for any of it: the option is off by default,
and without it SessionSet declines the split silently - `SPLIT V` still
answers OK and the pane count stays where it was. That refusal is a test
of its own below, so a future test that forgets the argument fails with
its own name rather than with a puzzling geometry.
"""

import unittest

from harness import Shitty


def widths(terminal):
    """Every visible pane's column count, in pane-index order."""
    return [terminal.winsize_pane(index)[0] for index in range(terminal.pane_count())]


def sizes(terminal):
    """Every visible pane's (columns, rows), in pane-index order."""
    return [terminal.winsize_pane(index) for index in range(terminal.pane_count())]


class PaneProtocolTest(unittest.TestCase):
    def test_a_fresh_window_shows_one_pane_the_size_of_the_window(self):
        with Shitty(columns=40, rows=10, extra_arguments=("-panes",)) as terminal:
            self.assertEqual(terminal.pane_count(), 1)
            self.assertEqual(terminal.winsize_pane(0), (40, 10))
            self.assertEqual(terminal.winsize_pane(0), terminal.winsize())

    def test_a_vertical_split_makes_two_panes_and_leaves_the_terminal_answering(self):
        with Shitty(columns=41, rows=11, extra_arguments=("-panes",)) as terminal:
            self.assertEqual(terminal.pane_count(), 1)
            terminal.split("V")
            # Before T1 this line never ran: the split left the harness
            # without a kit for the new session and killed the process.
            self.assertEqual(terminal.pane_count(), 2)
            self.assertEqual(terminal.session_state()[0], 2)

    def test_a_split_is_declined_without_the_panes_option(self):
        # SPLIT answers OK either way - the command published the action,
        # and SessionSet is the one that says no. The pane count is the
        # only place the refusal shows.
        with Shitty(columns=40, rows=10) as terminal:
            terminal.split("V")
            self.assertEqual(terminal.pane_count(), 1)
            with self.assertRaises(RuntimeError):
                terminal.winsize_pane(1)

    def test_a_vertical_split_divides_columns_and_leaves_rows_alone(self):
        # The formulation a pane test can rely on. "The two widths
        # differ" is not it: a split of an even window gives two equal
        # halves, and the criterion would then be a statement about the
        # window's parity rather than about the protocol.
        with Shitty(columns=41, rows=11, extra_arguments=("-panes",)) as terminal:
            terminal.split("V")
            self.assertEqual(terminal.pane_count(), 2)
            for columns, rows in sizes(terminal):
                self.assertLess(columns, 41)
                self.assertGreater(columns, 0)
                self.assertEqual(rows, 11)

    def test_an_even_window_splits_into_two_equal_widths(self):
        # Together with the odd case below, this is why "pane 1 is wider
        # than pane 0" must not be anyone's acceptance criterion: which
        # of the two holds is decided by the window's parity, not by the
        # protocol. Written as two tests so a change to the split
        # arithmetic says which half of the property moved.
        with Shitty(columns=40, rows=10, extra_arguments=("-panes",)) as terminal:
            terminal.split("V")
            left, right = widths(terminal)
            self.assertEqual(left, right)

    def test_an_odd_window_splits_into_two_unequal_widths(self):
        with Shitty(columns=41, rows=11, extra_arguments=("-panes",)) as terminal:
            terminal.split("V")
            left, right = widths(terminal)
            self.assertEqual(right - left, 1)

    def test_a_horizontal_split_divides_rows_and_leaves_columns_alone(self):
        with Shitty(columns=41, rows=11, extra_arguments=("-panes",)) as terminal:
            terminal.split("H")
            self.assertEqual(terminal.pane_count(), 2)
            for columns, rows in sizes(terminal):
                self.assertEqual(columns, 41)
                self.assertLess(rows, 11)
                self.assertGreater(rows, 0)

    def test_a_pane_born_from_a_split_is_born_with_its_own_geometry(self):
        # The pane a split creates never gets a resize after it is born:
        # applyLayout() only tells the panes that gave up room. So the
        # size its pty reports is the size spawn() was given, and a zero
        # here means the fake factory forgot to pass it to openpty().
        with Shitty(columns=41, rows=11, extra_arguments=("-panes",)) as terminal:
            terminal.split("V")
            columns, rows = terminal.winsize_pane(1)
            self.assertGreater(columns, 0)
            self.assertLess(columns, 41)
            self.assertEqual(rows, 11)

    def test_a_session_opened_in_a_new_tab_is_born_with_the_windows_grid(self):
        # Not a pane, but the same birth: every session after the first
        # gets its pty from openpty() inside the test factory, and K3
        # left that pty with nobody to resize it afterwards. A tab is the
        # one route where the birth size is the size that stays - a split
        # is followed by applyLayout(), which sizes every pane again and
        # would paper over a pty born at 0x0.
        with Shitty(columns=11, rows=7) as terminal:
            self.assertEqual(terminal.winsize(), (11, 7))
            terminal.new_session()
            self.assertEqual(terminal.winsize(), (11, 7))

    def test_pane_indices_follow_the_visual_layout_and_not_creation_order(self):
        # Split vertically, focus the left half, split that horizontally:
        # the pane created last sits at index 1, between the two halves
        # of the left column, because the index is a walk of the layout.
        # Creation order would have put it at index 2.
        with Shitty(columns=41, rows=11, extra_arguments=("-panes",)) as terminal:
            terminal.split("V")
            terminal.focus_pane(0)
            terminal.split("H")
            self.assertEqual(terminal.pane_count(), 3)
            (top_columns, top_rows), (bottom_columns, bottom_rows), (right_columns, right_rows) = sizes(terminal)
            # Indices 0 and 1 are the two halves of the left column, and
            # index 2 is the untouched right one. Creation order would
            # have put the newest pane last instead of between them.
            self.assertEqual(top_columns, bottom_columns)
            self.assertNotEqual(top_columns, right_columns)
            self.assertEqual(right_rows, 11)
            self.assertLess(top_rows, right_rows)
            self.assertLess(bottom_rows, right_rows)

    def test_each_pane_index_names_one_pane_and_the_same_one_every_time(self):
        # Tag every pane through its own index and read every pane back:
        # each sees its own tag and nobody else's. This is what makes an
        # index usable as an address at all.
        with Shitty(columns=41, rows=11, extra_arguments=("-panes",)) as terminal:
            terminal.split("V")
            terminal.focus_pane(0)
            terminal.split("H")
            count = terminal.pane_count()
            for index in range(count):
                terminal.write_to_pane(index, f"P{index}".encode("ascii"))
            for index in range(count):
                seen = terminal.screen_text_pane(index)
                for other in range(count):
                    tag = f"P{other}"
                    if other == index:
                        self.assertIn(tag, seen)
                    else:
                        self.assertNotIn(tag, seen)

    def test_output_written_to_one_pane_is_invisible_to_the_other(self):
        with Shitty(columns=41, rows=11, extra_arguments=("-panes",)) as terminal:
            terminal.split("V")
            terminal.write_to_pane(1, b"SECOND-PANE-ONLY")
            self.assertIn("SECOND-PANE-ONLY", terminal.screen_text_pane(1))
            self.assertNotIn("SECOND-PANE-ONLY", terminal.screen_text_pane(0))

    def test_focus_pane_moves_where_the_windows_own_output_lands(self):
        # WRITE addresses the session the window shows, so what it
        # reaches is the proof that FOCUS_PANE moved the focus rather
        # than merely answering OK.
        with Shitty(columns=41, rows=11, extra_arguments=("-panes",)) as terminal:
            terminal.split("V")
            terminal.focus_pane(0)
            terminal.write(b"LEFT")
            self.assertIn("LEFT", terminal.screen_text_pane(0))
            self.assertNotIn("LEFT", terminal.screen_text_pane(1))
            terminal.focus_pane(1)
            terminal.write(b"RIGHT")
            self.assertIn("RIGHT", terminal.screen_text_pane(1))
            self.assertNotIn("RIGHT", terminal.screen_text_pane(0))

    def test_a_pane_index_nothing_answers_to_is_a_command_error(self):
        # Every pane-addressed command, because they reach the index
        # check by four different routes, and the terminal has to be
        # alive after all four: a raiseError that escaped the command
        # loop's catch would take the process with it.
        with Shitty(columns=41, rows=11, extra_arguments=("-panes",)) as terminal:
            terminal.split("V")
            for reach in (
                lambda: terminal.winsize_pane(2),
                lambda: terminal.screen_text_pane(9),
                lambda: terminal.focus_pane(7),
                lambda: terminal.write_to_pane(5, b"x"),
            ):
                with self.assertRaises(RuntimeError) as raised:
                    reach()
                self.assertIn("no such pane", str(raised.exception))
            self.assertEqual(terminal.pane_count(), 2)

    def test_a_pane_command_without_its_argument_is_refused_by_name(self):
        # The argument checks the index checks never reach. Sent raw
        # because the harness wrappers always supply an argument, and
        # the terminal has to survive all five: these are the same
        # raiseError that a bad index takes, on a different path into
        # the command loop's catch.
        with Shitty(columns=20, rows=6, extra_arguments=("-panes",)) as terminal:
            for command, complaint in (
                ("SPLIT ", "direction"),
                ("FOCUS_PANE ", "index"),
                ("WINSIZE_PANE ", "index"),
                ("SCREEN_TEXT_PANE ", "index"),
                ("PTY_WRITE_PANE ", "pane write"),
                ("PTY_WRITE_PANE 0", "pane write"),
            ):
                with self.assertRaises(RuntimeError) as raised:
                    terminal.command(command)
                self.assertIn(complaint, str(raised.exception))
            self.assertEqual(terminal.pane_count(), 1)

    def test_a_split_direction_other_than_v_or_h_is_refused(self):
        with Shitty(columns=41, rows=11, extra_arguments=("-panes",)) as terminal:
            with self.assertRaises(RuntimeError):
                terminal.split("X")
            self.assertEqual(terminal.pane_count(), 1)

    def test_each_tab_counts_and_addresses_only_its_own_panes(self):
        # Two tabs of two panes. The pane index is a position in the
        # active tab's layout, so the same index names a different
        # terminal depending on which tab is in front - and a pane in the
        # background tab must not hear what its namesake was told.
        with Shitty(columns=41, rows=11, extra_arguments=("-panes",)) as terminal:
            terminal.split("V")
            terminal.write_to_pane(0, b"FIRST-TAB-LEFT")
            terminal.write_to_pane(1, b"FIRST-TAB-RIGHT")

            terminal.new_session()
            self.assertEqual(terminal.pane_count(), 1)
            terminal.split("V")
            self.assertEqual(terminal.pane_count(), 2)
            terminal.write_to_pane(0, b"SECOND-TAB-LEFT")
            terminal.write_to_pane(1, b"SECOND-TAB-RIGHT")
            self.assertIn("SECOND-TAB-LEFT", terminal.screen_text_pane(0))
            self.assertNotIn("FIRST-TAB-LEFT", terminal.screen_text_pane(0))

            terminal.chord_prev_tab()
            self.assertEqual(terminal.pane_count(), 2)
            self.assertIn("FIRST-TAB-LEFT", terminal.screen_text_pane(0))
            self.assertIn("FIRST-TAB-RIGHT", terminal.screen_text_pane(1))
            self.assertNotIn("SECOND-TAB-LEFT", terminal.screen_text_pane(0))
            self.assertEqual(terminal.session_state()[0], 4)

    def test_a_window_too_small_to_divide_evenly_still_splits(self):
        # The smallest window the harness will open. Both panes have to
        # come out with a usable grid; a zero column count here would be
        # a pane whose child cannot draw anything at all.
        with Shitty(columns=2, rows=2, extra_arguments=("-panes",)) as terminal:
            terminal.split("V")
            self.assertEqual(terminal.pane_count(), 2)
            for columns, rows in sizes(terminal):
                self.assertGreater(columns, 0)
                self.assertGreater(rows, 0)


if __name__ == "__main__":
    unittest.main()
