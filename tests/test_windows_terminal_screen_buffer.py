# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import re
from pathlib import Path
import unittest

from harness import Shitty


UPSTREAM = (
    Path(__file__).parent
    / "windows_terminal"
    / "upstream"
    / "ScreenBufferTests.cpp"
)

PORTED_METHODS = {
    "SingleAlternateBufferCreationTest",
    "MultipleAlternateBufferCreationTest",
    "MultipleAlternateBuffersFromMainCreationTest",
    "AlternateBufferCursorInheritanceTest",
    "TestReverseLineFeed",
    "TestResetClearTabStops",
    "TestAddTabStop",
    "TestClearTabStop",
    "TestGetForwardTab",
    "TestGetReverseTab",
    "TestAltBufferTabStops",
    "EraseAllTests",
    "InactiveControlCharactersTest",
}


def upstream_methods():
    source = UPSTREAM.read_text()
    return set(re.findall(r"TEST_METHOD\((\w+)\);", source))


def tab_columns(terminal, columns):
    return tuple(
        column
        for column, present in enumerate(terminal.tab_stops(columns))
        if present
    )


def replace_tab_stops(terminal, columns):
    terminal.write(b"\x1b[3g")
    for column in columns:
        terminal.write(f"\x1b[{column + 1}G\x1bH".encode())


class WindowsTerminalScreenBufferInitialTest(unittest.TestCase):
    def test_upstream_inventory_has_all_113_methods(self):
        methods = upstream_methods()
        self.assertEqual(len(methods), 113)
        self.assertLessEqual(PORTED_METHODS, methods)

    def test_single_alternate_buffer_creation(self):
        with Shitty(columns=10, rows=4) as terminal:
            terminal.write(b"main\x1b[?1049h")
            alternate = terminal.snapshot()
            self.assertEqual(alternate.lines, [" " * 10] * 4)

            terminal.write(b"alternate\x1b[?1049l")
            primary = terminal.snapshot()
            self.assertEqual(primary.lines[0], "main      ")
            self.assertEqual((primary.cursor_x, primary.cursor_y), (4, 0))

    def test_multiple_alternate_buffer_creation(self):
        with Shitty(columns=10, rows=4) as terminal:
            terminal.write(
                b"main\x1b[?47hfirst\x1b[?47h-second\x1b[?47l"
            )
            primary = terminal.snapshot()
            self.assertEqual(primary.lines[0], "main      ")

            terminal.write(b"\x1b[?47h")
            alternate = terminal.snapshot()
            self.assertEqual(alternate.lines[0], "    first-")
            self.assertEqual(alternate.lines[1], "second    ")

    def test_multiple_alternate_buffers_from_main_creation(self):
        with Shitty(columns=10, rows=4) as terminal:
            terminal.write(
                b"main"
                b"\x1b[?1047hfirst"
                b"\x1b[?1047l"
                b"\x1b[?1047hsecond"
                b"\x1b[?1047l"
            )
            self.assertEqual(terminal.snapshot().lines[0], "main      ")

            terminal.write(b"\x1b[?1047h")
            self.assertEqual(terminal.snapshot().lines, [" " * 10] * 4)

    def test_alternate_buffer_cursor_inheritance(self):
        with Shitty(columns=10, rows=6) as terminal:
            terminal.write(
                b"\x1b[3;4H\x1b[?25l\x1b[5 q"
                b"\x1b[?1049h"
            )
            alternate = terminal.snapshot()
            self.assertEqual((alternate.cursor_x, alternate.cursor_y), (3, 2))
            self.assertEqual(alternate.cursor_style, 0)

            terminal.write(
                b"\x1b[2;6H\x1b[?25h\x1b[3 q"
                b"\x1b[?1049l"
            )
            primary = terminal.snapshot()
            self.assertEqual((primary.cursor_x, primary.cursor_y), (3, 2))
            self.assertNotEqual(primary.cursor_style, 0)

    def test_reverse_line_feed(self):
        with Shitty(columns=10, rows=4) as terminal:
            terminal.write(b"foo\nfoo\x1bM")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (6, 0))

        with Shitty(columns=10, rows=4) as terminal:
            terminal.write(b"123456789\x1bM")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (9, 0))
            self.assertEqual(snapshot.lines[0], " " * 10)
            self.assertEqual(snapshot.lines[1], "123456789 ")

    def test_reset_clear_tab_stops(self):
        expected = (0, *range(8, 80, 8))
        with Shitty(columns=80, rows=2) as terminal:
            self.assertEqual(tab_columns(terminal, 80), expected)

            terminal.write(b"\x1b[3g")
            self.assertEqual(tab_columns(terminal, 80), ())

            terminal.write(b"\x1bc")
            self.assertEqual(tab_columns(terminal, 80), expected)

            terminal.write(b"\x1b[3g\x1b[?5W")
            self.assertEqual(tab_columns(terminal, 80), expected)

            terminal.write(b"\x1b[3g\x1b[?W")
            self.assertEqual(tab_columns(terminal, 80), expected)

    def test_add_tab_stop(self):
        with Shitty(columns=40, rows=2) as terminal:
            terminal.write(b"\x1b[3g")
            expected = []
            for column in (12, 4, 30, 24, 24):
                terminal.write(f"\x1b[{column + 1}G\x1bH".encode())
                if column not in expected:
                    expected.append(column)
                    expected.sort()
                self.assertEqual(tab_columns(terminal, 40), tuple(expected))

    def test_clear_tab_stop(self):
        with Shitty(columns=40, rows=2) as terminal:
            terminal.write(b"\x1b[3g\x1b[g")
            self.assertEqual(tab_columns(terminal, 40), ())

            terminal.write(b"\x1b[H\x1b[g")
            self.assertEqual(tab_columns(terminal, 40), ())

            replace_tab_stops(terminal, (1,))
            terminal.write(b"\x1b[3G\x1b[g\x1b[1G\x1b[g")
            self.assertEqual(tab_columns(terminal, 40), (1,))

            for removed in (3, 5, 17, 0):
                expected = [3, 5, 6, 10, 15, 17]
                replace_tab_stops(terminal, expected)
                terminal.write(f"\x1b[{removed + 1}G\x1b[g".encode())
                if removed in expected:
                    expected.remove(removed)
                self.assertEqual(tab_columns(terminal, 40), tuple(expected))

    def test_get_forward_tab(self):
        with Shitty(columns=40, rows=2) as terminal:
            replace_tab_stops(terminal, (3, 5, 6, 10, 15, 17))
            for start, expected in ((0, 3), (6, 10), (30, 39), (39, 39)):
                terminal.write(f"\x1b[{start + 1}G\x1b[I".encode())
                self.assertEqual(terminal.snapshot().cursor_x, expected)

    def test_get_reverse_tab(self):
        with Shitty(columns=40, rows=2) as terminal:
            replace_tab_stops(terminal, (3, 5, 6, 10, 15, 17))
            for start, expected in ((1, 0), (6, 5), (30, 17)):
                terminal.write(f"\x1b[{start + 1}G\x1b[Z".encode())
                self.assertEqual(terminal.snapshot().cursor_x, expected)

    def test_alternate_buffer_tab_stops(self):
        with Shitty(columns=40, rows=2) as terminal:
            replace_tab_stops(terminal, (3, 5, 6, 10, 15, 17))
            terminal.write(b"\x1b[?47h")
            self.assertEqual(
                tab_columns(terminal, 40), (3, 5, 6, 10, 15, 17)
            )

            replace_tab_stops(terminal, (4, 8, 12, 16))
            terminal.write(b"\x1b[?47l")
            self.assertEqual(tab_columns(terminal, 40), (4, 8, 12, 16))

    def test_erase_all_uses_terminal_not_win32_viewport_semantics(self):
        with Shitty(columns=10, rows=4) as terminal:
            terminal.write(b"foo\r\nbar\x1b[2J")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, [" " * 10] * 4)
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (3, 1))

    def test_inactive_control_characters(self):
        controls = (
            0, 1, 2, 3, 4, 5, 6, 7,
            14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
            28, 29, 30, 31,
        )
        for ordinal in controls:
            with self.subTest(ordinal=ordinal), Shitty(
                columns=10, rows=3
            ) as terminal:
                control = bytes((ordinal,))
                terminal.write(control)
                self.assertEqual(
                    (terminal.snapshot().cursor_x,
                     terminal.snapshot().cursor_y),
                    (0, 0),
                )

                terminal.write(control * 8)
                self.assertEqual(
                    (terminal.snapshot().cursor_x,
                     terminal.snapshot().cursor_y),
                    (0, 0),
                )

                terminal.write(control + b"foo\r\n")
                terminal.write(
                    control + b"foo" + control + b"bar" + control
                )
                snapshot = terminal.snapshot()
                self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (6, 1))
