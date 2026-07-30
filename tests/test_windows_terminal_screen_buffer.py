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
    "VtResize",
    "VtResizeComprehensive",
    "VtResizeDECCOLM",
    "VtResizePreservingAttributes",
    "VtSoftResetCursorPosition",
    "VtSoftResetAltBufferCursorState",
    "VtScrollMarginsNewlineColor",
    "VtNewlinePastViewport",
    "VtNewlinePastEndOfBuffer",
    "VtNewlineOutsideMargins",
    "VtSetColorTable",
    "VtRestoreColorTableReport",
    "ResizeTraditionalDoesNotDoubleFreeAttrRows",
    "ResizeCursorUnchanged",
    "ResizeAltBuffer",
    "ResizeAltBufferGetScreenBufferInfo",
    "VtEraseAllPersistCursor",
    "VtEraseAllPersistCursorFillColor",
    "GetWordBoundary",
    "TestAltBufferCursorState",
    "TestAltBufferVtDispatching",
    "TestAltBufferRIS",
}

CLASSIFIED_METHODS = {
    "GetWordBoundaryTrimZerosOn",
    "GetWordBoundaryTrimZerosOff",
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


def window_terminal(columns=10, rows=4):
    return Shitty(
        columns=columns,
        rows=rows,
        extra_arguments=("-allowWindowOps", "true"),
    )


def palette_color(terminal, index):
    terminal.write(f"\x1b]4;{index};?\x1b\\".encode())
    reply = terminal.read_input()
    match = re.fullmatch(
        rb"\x1b\]4;" + str(index).encode()
        + rb";rgb:([0-9a-f]{4})/([0-9a-f]{4})/([0-9a-f]{4})\x1b\\",
        reply,
    )
    if match is None:
        raise AssertionError(f"invalid palette reply: {reply!r}")
    return tuple(int(component[:2], 16) for component in match.groups())


def select_word(terminal, column, row=0):
    x = column + 2
    y = row + 2
    terminal.button(0, True, x=x, y=y, time=1.0)
    terminal.button(0, False, x=x, y=y, time=1.01)
    terminal.button(0, True, x=x, y=y, time=1.1)
    return terminal.button(0, False, x=x, y=y, time=1.11)


class WindowsTerminalScreenBufferInitialTest(unittest.TestCase):
    def test_upstream_inventory_has_all_113_methods(self):
        methods = upstream_methods()
        self.assertEqual(len(methods), 113)
        self.assertLessEqual(PORTED_METHODS, methods)
        self.assertLessEqual(CLASSIFIED_METHODS, methods)
        self.assertFalse(PORTED_METHODS & CLASSIFIED_METHODS)

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


class WindowsTerminalScreenBufferResizeAndColorTest(unittest.TestCase):
    def test_vt_resize(self):
        with window_terminal() as terminal:
            for rows, columns in ((30, 80), (40, 80), (40, 90), (12, 12)):
                terminal.write(f"\x1b[8;{rows};{columns}t".encode())
                snapshot = terminal.snapshot()
                self.assertEqual((snapshot.columns, snapshot.rows), (columns, rows))

            terminal.write(b"\x1b[8;0;0t")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.columns, snapshot.rows), (1920, 1080))

    def test_vt_resize_comprehensive(self):
        for delta_columns in (-10, -1, 0, 1, 10):
            for delta_rows in (-10, -1, 0, 1, 10):
                with self.subTest(
                    delta_columns=delta_columns,
                    delta_rows=delta_rows,
                ), window_terminal(columns=80, rows=24) as terminal:
                    columns = 80 + delta_columns
                    rows = 24 + delta_rows
                    terminal.write(f"\x1b[8;{rows};{columns}t".encode())
                    snapshot = terminal.snapshot()
                    self.assertEqual(
                        (snapshot.columns, snapshot.rows),
                        (columns, rows),
                    )

    def test_vt_resize_deccolm(self):
        with Shitty(columns=80, rows=24) as terminal:
            terminal.write(b"\x1b[5;15r\x1b[10;40HABCDEF\x1b[?3h")
            unchanged = terminal.snapshot()
            self.assertEqual((unchanged.columns, unchanged.rows), (80, 24))
            self.assertEqual(
                (unchanged.cursor_x, unchanged.cursor_y),
                (45, 9),
            )

            terminal.write(b"\x1b[?40h\x1b[?3h")
            wide = terminal.snapshot()
            self.assertEqual((wide.columns, wide.rows), (132, 24))
            self.assertEqual((wide.cursor_x, wide.cursor_y), (0, 0))
            terminal.write(b"\x1b[9999B")
            self.assertEqual(terminal.snapshot().cursor_y, 23)

            terminal.write(
                b"\x1b[5;15r\x1b[10;40HABCDEF\x1b[?40l\x1b[?3l"
            )
            disallowed = terminal.snapshot()
            self.assertEqual((disallowed.columns, disallowed.rows), (132, 24))
            self.assertEqual(
                (disallowed.cursor_x, disallowed.cursor_y),
                (45, 9),
            )

            terminal.write(b"\x1b[?40h\x1b[?3l")
            narrow = terminal.snapshot()
            self.assertEqual((narrow.columns, narrow.rows), (80, 24))
            self.assertEqual((narrow.cursor_x, narrow.cursor_y), (0, 0))
            terminal.write(b"\x1b[9999B")
            self.assertEqual(terminal.snapshot().cursor_y, 23)

    def test_vt_resize_preserving_attributes(self):
        attributes = (
            b"\x1b[3;4:3;9;"
            b"38;2;12;34;56;48;2;78;90;12;58;2;188;20;24m"
        )
        for resize in ("host", "deccolm"):
            with self.subTest(resize=resize), Shitty(
                columns=80,
                rows=24,
            ) as terminal:
                terminal.write(attributes)
                before = terminal.pen_state()
                if resize == "host":
                    terminal.resize(132, 24)
                    terminal.resize(80, 24)
                else:
                    terminal.write(b"\x1b[?40h\x1b[?3h\x1b[?3l")
                self.assertEqual(terminal.pen_state(), before)

    def test_vt_soft_reset_cursor_position(self):
        with Shitty(columns=20, rows=12) as terminal:
            terminal.write(b"\x1b[2;2H\x1b[!p")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (1, 1))

            terminal.write(b"\x1b[2;10r")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 0))

            terminal.write(b"\x1b[2;2H\x1b[!p")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (1, 1))

            terminal.write(b"\x1b[?6h\x1b[5;10r\x1b[2;2H")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (1, 5))

            terminal.write(b"\x1b[!p\x1b[5;10r\x1b[2;2H")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (1, 1))

    def test_vt_soft_reset_alt_buffer_cursor_state(self):
        with Shitty(columns=12, rows=6) as terminal:
            terminal.write(
                b"\x1b[4;7H"
                b"\x1b[?1049h\x1b[!p\x1b[?1049l"
            )
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (6, 3))

    def test_vt_scroll_margins_newline_color(self):
        with Shitty(columns=12, rows=10) as terminal:
            terminal.write(
                b"\x1b]10;#ffff00\x1b\\"
                b"\x1b]11;#ff00ff\x1b\\"
                b"\x1b[2J\x1b[m\x1b[2;5r"
            )
            for _ in range(10):
                terminal.write(b"X\n")
                snapshot = terminal.snapshot()
                for cell in snapshot.cells:
                    self.assertEqual(cell.foreground, (255, 255, 0))
                    self.assertEqual(cell.background, (255, 0, 255))

    def assert_bottom_row_uses_erase_colors(self, terminal):
        snapshot = terminal.snapshot()
        self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 3))
        for column in range(snapshot.columns):
            cell = snapshot.cell(column, 3)
            self.assertEqual(cell.foreground, (12, 34, 56))
            self.assertEqual(cell.background, (78, 90, 12))
            self.assertFalse(cell.italic)
            self.assertFalse(cell.underline)
            self.assertFalse(cell.inverse)
            self.assertFalse(cell.strike)

    def test_vt_newline_past_viewport(self):
        with Shitty(columns=8, rows=4) as terminal:
            terminal.write(
                b"\x1b[2J\x1b[4;1H"
                b"\x1b[3;4:3;7;9;"
                b"38;2;12;34;56;48;2;78;90;12m\n"
            )
            self.assert_bottom_row_uses_erase_colors(terminal)

    def test_vt_newline_past_end_of_buffer(self):
        with Shitty(columns=8, rows=4, save_lines=4) as terminal:
            terminal.write(
                b"\x1b[2J"
                + b"\n" * 16
                + b"\x1b[3;4:3;7;9;"
                b"38;2;12;34;56;48;2;78;90;12m\n"
            )
            self.assert_bottom_row_uses_erase_colors(terminal)

    def test_vt_newline_outside_margins(self):
        with Shitty(columns=8, rows=8) as terminal:
            terminal.write(b"\x1b[8;1H\n")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 7))

            terminal.write(b"\x1b[1;5r\x1b[8;1H\n")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 7))

    def test_vt_set_color_table(self):
        valid = (
            (0, b"rgb:1/1/1", (0x11, 0x11, 0x11), b"\x07"),
            (1, b"rgb:1/23/1", (0x11, 0x23, 0x11), b"\x07"),
            (2, b"rgb:1/23/12", (0x11, 0x23, 0x12), b"\x07"),
            (3, b"rgb:12/23/12", (0x12, 0x23, 0x12), b"\x07"),
            (4, b"rgb:ff/a1/1b", (0xFF, 0xA1, 0x1B), b"\x07"),
            (5, b"rgb:ff/a1/1b", (0xFF, 0xA1, 0x1B), b"\x1b\\"),
        )
        invalid = (
            b"rgb:/1/1",
            b"rgb:1/1/1/1",
            b"rgb:1//1",
            b"rgb://",
            b"rgb:1/11/",
            b"cmyk:1/1/1",
            b"1/1/1",
        )
        with Shitty(columns=4, rows=2) as terminal:
            for index, color, expected, terminator in valid:
                terminal.write(
                    b"\x1b]4;" + str(index).encode() + b";"
                    + color + terminator
                )
                self.assertEqual(palette_color(terminal, index), expected)

            terminal.write(b"\x1b]4;5;rgb:09/09/09\x1b\\")
            for color in invalid:
                terminal.write(b"\x1b]4;5;" + color + b"\x1b\\")
                self.assertEqual(palette_color(terminal, 5), (9, 9, 9))
            terminal.write(b"\x1b]4;5;rgbi:1/1/1\x1b\\")
            self.assertEqual(palette_color(terminal, 5), (255, 255, 255))
            terminal.write(b"\x1b]4;;rgb:1/1/1\x1b\\")
            self.assertEqual(palette_color(terminal, 5), (255, 255, 255))

    def test_vt_restore_color_table_report(self):
        hls = (
            (0, 0, 0, 0, (0, 0, 0)),
            (1, 0, 49, 59, (51, 51, 199)),
            (2, 120, 46, 71, (201, 34, 34)),
            (3, 240, 49, 59, (51, 199, 51)),
            (4, 60, 49, 59, (199, 51, 199)),
            (5, 300, 49, 59, (51, 199, 199)),
            (6, 180, 49, 59, (199, 199, 51)),
            (7, 0, 46, 0, (117, 117, 117)),
            (8, 0, 26, 0, (66, 66, 66)),
            (9, 0, 46, 28, (84, 84, 150)),
            (10, 120, 42, 38, (148, 66, 66)),
            (11, 240, 46, 28, (84, 150, 84)),
            (12, 60, 46, 28, (150, 84, 150)),
            (13, 300, 46, 28, (84, 150, 150)),
            (14, 180, 46, 28, (150, 150, 84)),
            (15, 0, 79, 0, (201, 201, 201)),
        )
        rgb = (
            (0, 0, 0, 0, (0, 0, 0)),
            (1, 20, 20, 78, (51, 51, 199)),
            (2, 79, 13, 13, (201, 33, 33)),
            (3, 20, 78, 20, (51, 199, 51)),
            (4, 78, 20, 78, (199, 51, 199)),
            (5, 20, 78, 78, (51, 199, 199)),
            (6, 78, 78, 20, (199, 199, 51)),
            (7, 46, 46, 46, (117, 117, 117)),
            (8, 26, 26, 26, (66, 66, 66)),
            (9, 33, 33, 59, (84, 84, 150)),
            (10, 58, 26, 26, (148, 66, 66)),
            (11, 33, 59, 33, (84, 150, 84)),
            (12, 59, 33, 59, (150, 84, 150)),
            (13, 33, 59, 59, (84, 150, 150)),
            (14, 59, 59, 33, (150, 150, 84)),
            (15, 79, 79, 79, (201, 201, 201)),
        )
        with Shitty(columns=4, rows=2) as terminal:
            original = tuple(
                palette_color(terminal, index) for index in range(16)
            )
            for color_space, cases in ((1, hls), (2, rgb)):
                for index, first, second, third, expected in cases:
                    terminal.write(
                        f"\x1bP2$p{index};{color_space};"
                        f"{first};{second};{third}\x1b\\".encode()
                    )
                    self.assertEqual(palette_color(terminal, index), expected)

            terminal.write(
                b"\x1bP2$p"
                b"0;1;120;50;100/2;1;240;50;100/4;1;360;50;100"
                b"\x1b\\"
                b"\x1bP2$p"
                b"1;2;100;0;0/3;2;0;100;0/5;2;0;0;100"
                b"\x1b\\"
            )
            for index, expected in enumerate(
                (
                    (255, 0, 0),
                    (255, 0, 0),
                    (0, 255, 0),
                    (0, 255, 0),
                    (0, 0, 255),
                    (0, 0, 255),
                )
            ):
                self.assertEqual(palette_color(terminal, index), expected)

            omitted_and_clamped = (
                (6, b"1;;50;100", (0, 0, 255)),
                (7, b"1;120;;100", (0, 0, 0)),
                (8, b"1;120;50", (128, 128, 128)),
                (6, b"2;;50;100", (0, 128, 255)),
                (7, b"2;50;;100", (128, 0, 255)),
                (8, b"2;50;100", (128, 255, 0)),
                (9, b"1;480;50;100", (255, 0, 0)),
                (10, b"1;240;150;100", (255, 255, 255)),
                (11, b"1;0;50;120", (0, 0, 255)),
                (12, b"2;150;0;0", (255, 0, 0)),
                (13, b"2;0;150;0", (0, 255, 0)),
                (14, b"2;0;0;150", (0, 0, 255)),
            )
            for index, definition, expected in omitted_and_clamped:
                terminal.write(
                    b"\x1bP2$p" + str(index).encode() + b";"
                    + definition + b"\x1b\\"
                )
                self.assertEqual(palette_color(terminal, index), expected)

            terminal.write(b"\x1bc")
            self.assertEqual(
                tuple(
                    palette_color(terminal, index)
                    for index in range(16)
                ),
                original,
            )


class WindowsTerminalScreenBufferResizeEraseAndAltTest(unittest.TestCase):
    def test_resize_traditional_does_not_double_free_attr_rows(self):
        with Shitty(columns=12, rows=6, save_lines=0) as terminal:
            terminal.write(b"\x1b[31;44mone\r\ntwo\r\nthree")
            terminal.resize(12, 5)
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.columns, snapshot.rows), (12, 5))
            self.assertIn("three", "".join(snapshot.lines))

    def test_resize_cursor_unchanged(self):
        for alternate in (False, True):
            with self.subTest(alternate=alternate), Shitty(
                columns=80,
                rows=24,
            ) as terminal:
                terminal.write(b"\x1b[5 q")
                if alternate:
                    terminal.write(b"\x1b[?1049h")
                expected_style = terminal.snapshot().cursor_style
                self.assertNotEqual(expected_style, 0)
                for delta_columns in (-10, -1, 0, 1, 10):
                    for delta_rows in (-10, -1, 0, 1, 10):
                        terminal.resize(
                            80 + delta_columns,
                            24 + delta_rows,
                        )
                        self.assertEqual(
                            terminal.snapshot().cursor_style,
                            expected_style,
                        )
                if alternate:
                    terminal.write(b"\x1b[?1049l")
                    self.assertEqual(
                        terminal.snapshot().cursor_style,
                        expected_style,
                    )

    def test_resize_alt_buffer(self):
        with Shitty(columns=10, rows=4) as terminal:
            terminal.write(b"primary\x1b[?1049h\x1b[Halt")
            terminal.resize(12, 6)
            alternate = terminal.snapshot()
            self.assertEqual(
                (alternate.columns, alternate.rows),
                (12, 6),
            )
            self.assertEqual(alternate.lines[0], "alt" + " " * 9)

            terminal.write(b"\x1b[?1049l")
            primary = terminal.snapshot()
            self.assertEqual((primary.columns, primary.rows), (12, 6))
            self.assertEqual(primary.lines[0], "primary" + " " * 5)

    def test_resize_alt_buffer_get_screen_buffer_info(self):
        with Shitty(columns=80, rows=24) as terminal:
            terminal.write(b"\x1b[?1049h")
            for delta_columns in (-10, -1, 1, 10):
                for delta_rows in (-10, -1, 1, 10):
                    columns = 80 + delta_columns
                    rows = 24 + delta_rows
                    terminal.resize(columns, rows)
                    snapshot = terminal.snapshot()
                    self.assertEqual(
                        (snapshot.columns, snapshot.rows),
                        (columns, rows),
                    )
                    self.assertEqual(terminal.winsize(), (columns, rows))

    def test_vt_erase_all_persist_cursor(self):
        with Shitty(columns=10, rows=4) as terminal:
            terminal.write(b"\x1b[2;2H\x1b[2J")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (1, 1))
            self.assertEqual(snapshot.lines, [" " * 10] * 4)

    def test_vt_erase_all_persist_cursor_fill_color(self):
        with Shitty(columns=10, rows=4) as terminal:
            terminal.write(b"\x1b[31;104mtext\x1b[2J")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (4, 0))
            for cell in snapshot.cells:
                self.assertEqual(cell.char, " ")
                self.assertEqual(cell.foreground, (205, 0, 0))
                self.assertEqual(cell.background, (92, 92, 255))

    def test_get_word_boundary(self):
        text = b"This is some test text for word boundaries."
        cases = (
            (0, b"This"),
            (1, b"This"),
            (3, b"This"),
            (13, b"test"),
            (15, b"test"),
            (16, b"test"),
            (32, b"boundaries"),
            (39, b"boundaries"),
            (41, b"boundaries"),
            (12, b" "),
        )
        for column, expected in cases:
            with self.subTest(column=column), Shitty(
                columns=len(text),
                rows=2,
            ) as terminal:
                terminal.write(text)
                self.assertEqual(select_word(terminal, column), expected)

    def test_alt_buffer_vt_dispatching(self):
        with Shitty(columns=10, rows=6) as terminal:
            terminal.write(
                b"P"
                b"\x1b[?1049h"
                b"\x1b[5;6H\x1b[48;2;255;0;255mX"
            )
            alternate = terminal.snapshot()
            self.assertEqual((alternate.cursor_x, alternate.cursor_y), (6, 4))
            self.assertEqual(alternate.cell(5, 4).char, "X")
            self.assertEqual(alternate.cell(5, 4).background, (255, 0, 255))

            terminal.write(b"\x1b[?1049l")
            primary = terminal.snapshot()
            self.assertEqual((primary.cursor_x, primary.cursor_y), (1, 0))
            self.assertEqual(primary.lines[0], "P" + " " * 9)

    def test_alt_buffer_ris(self):
        with Shitty(columns=10, rows=4) as terminal:
            terminal.write(b"primary\x1b[?1049halt\x1bc")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 0))
            self.assertEqual(snapshot.lines, [" " * 10] * 4)

            terminal.write(b"\x1b[?47h")
            self.assertEqual(terminal.snapshot().lines, [" " * 10] * 4)
