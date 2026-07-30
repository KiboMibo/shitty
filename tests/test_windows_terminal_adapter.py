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
    / "AdapterTest.cpp"
)

PORTED_METHODS = {
    "CursorMovementTest",
    "CursorPositionTest",
    "CursorSingleDimensionMoveTest",
    "CursorSaveRestoreTest",
    "CursorHideShowTest",
    "GraphicsBaseTests",
    "GraphicsSingleTests",
    "GraphicsSingleWithSubParamTests",
    "GraphicsPushPopTests",
    "GraphicsPersistBrightnessTests",
    "DeviceStatus_OperatingStatusTests",
    "DeviceStatus_CursorPositionReportTests",
    "DeviceStatus_ExtendedCursorPositionReportTests",
    "DeviceStatus_PrivateStatusTests",
    "DeviceAttributesTests",
    "SecondaryDeviceAttributesTests",
    "TertiaryDeviceAttributesTests",
    "RequestDisplayedExtentTests",
    "RequestTerminalParametersTests",
    "RequestStandardModeTests",
    "RequestPrivateModeTests",
    "RequestPermanentModeTests",
    "RequestSettingsTests",
}

CLASSIFIED_METHODS = {
    "DeviceStatus_MacroSpaceReportTest": "DEC macro storage is not implemented",
    "DeviceStatus_MemoryChecksumReportTest": "DEC macro storage is not implemented",
}


def upstream_methods():
    return set(re.findall(r"TEST_METHOD\((\w+)\)", UPSTREAM.read_text()))


class WindowsTerminalAdapterCursorTest(unittest.TestCase):
    def test_upstream_inventory_has_53_methods(self):
        methods = upstream_methods()
        self.assertEqual(len(methods), 53)
        self.assertLessEqual(PORTED_METHODS, methods)
        self.assertLessEqual(CLASSIFIED_METHODS.keys(), methods)
        self.assertFalse(PORTED_METHODS & CLASSIFIED_METHODS.keys())

    def test_cursor_movement(self):
        movements = {
            b"A": ((5, 3), (5, 2), (5, 0)),
            b"B": ((5, 3), (5, 4), (5, 5)),
            b"C": ((5, 3), (6, 3), (9, 3)),
            b"D": ((5, 3), (4, 3), (0, 3)),
            b"E": ((5, 3), (0, 4), (0, 5)),
            b"F": ((5, 3), (0, 2), (0, 0)),
        }
        for final, (start, one, bounded) in movements.items():
            with self.subTest(final=final):
                with Shitty(columns=10, rows=6) as terminal:
                    terminal.write(
                        f"\x1b[{start[1] + 1};{start[0] + 1}H".encode()
                        + b"\x1b["
                        + final
                    )
                    self.assertEqual(
                        (
                            terminal.snapshot().cursor_x,
                            terminal.snapshot().cursor_y,
                        ),
                        one,
                    )
                    terminal.write(b"\x1b[100" + final)
                    self.assertEqual(
                        (
                            terminal.snapshot().cursor_x,
                            terminal.snapshot().cursor_y,
                        ),
                        bounded,
                    )

        for final, position, expected in (
            (b"A", (4, 0), (4, 0)),
            (b"B", (4, 5), (4, 5)),
            (b"C", (9, 2), (9, 2)),
            (b"D", (0, 2), (0, 2)),
            (b"E", (9, 5), (0, 5)),
            (b"F", (9, 0), (0, 0)),
        ):
            with self.subTest(boundary=final):
                with Shitty(columns=10, rows=6) as terminal:
                    terminal.write(
                        f"\x1b[{position[1] + 1};{position[0] + 1}H".encode()
                        + b"\x1b["
                        + final
                    )
                    snapshot = terminal.snapshot()
                    self.assertEqual(
                        (snapshot.cursor_x, snapshot.cursor_y), expected
                    )

    def test_cursor_position(self):
        with Shitty(columns=10, rows=6) as terminal:
            for sequence, expected in (
                (b"\x1b[3;5H", (4, 2)),
                (b"\x1b[1;1H", (0, 0)),
                (b"\x1b[100;100H", (9, 5)),
            ):
                terminal.write(sequence)
                snapshot = terminal.snapshot()
                self.assertEqual(
                    (snapshot.cursor_x, snapshot.cursor_y), expected
                )

    def test_cursor_single_dimension_move(self):
        with Shitty(columns=10, rows=6) as terminal:
            terminal.write(b"\x1b[4;4H\x1b[6G")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (5, 3))

            terminal.write(b"\x1b[1G\x1b[100G")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (9, 3))

            terminal.write(b"\x1b[4;4H\x1b[5d")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (3, 4))

            terminal.write(b"\x1b[1d\x1b[100d")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (3, 5))

    def test_cursor_save_restore(self):
        with Shitty(columns=10, rows=6) as terminal:
            terminal.write(b"\x1b8")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (0, 0))

            terminal.write(
                b"\x1b[4;6H\x1b[1;3;4;31;42m\x1b7"
                b"\x1b[1;1H\x1b[0m\x1b8X"
            )
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (6, 3))
            restored = terminal.model_snapshot().cell(5, 3)
            self.assertTrue(restored.bold)
            self.assertTrue(restored.italic)
            self.assertTrue(restored.underline)
            self.assertEqual(restored.foreground_index, 9)
            self.assertEqual(restored.background_index, 2)

    def test_cursor_hide_show(self):
        with Shitty(columns=10, rows=6) as terminal:
            for sequence, expected in (
                (b"\x1b[?25l", 0),
                (b"\x1b[?25l", 0),
                (b"\x1b[?25h", 1),
                (b"\x1b[?25h", 1),
            ):
                terminal.write(sequence)
                self.assertEqual(terminal.cursor_state()[0], expected)


class WindowsTerminalAdapterGraphicsTest(unittest.TestCase):
    def test_graphics_base(self):
        with Shitty(columns=4, rows=2) as terminal:
            terminal.write(b"\x1b[mX")
            cell = terminal.snapshot().cell(0, 0)
            self.assertFalse(
                cell.bold
                or cell.faint
                or cell.italic
                or cell.underline
                or cell.inverse
                or cell.conceal
                or cell.strike
                or cell.overline
            )

    def test_graphics_single_attributes(self):
        enables = {
            1: ("bold", True),
            2: ("faint", True),
            4: ("underline_style", 1),
            7: ("inverse", True),
            8: ("conceal", True),
            9: ("strike", True),
            21: ("underline_style", 2),
            53: ("overline", True),
        }
        for code, (field, expected) in enables.items():
            with self.subTest(code=code):
                with Shitty(columns=4, rows=2) as terminal:
                    terminal.write(f"\x1b[{code}mX".encode())
                    self.assertEqual(
                        getattr(terminal.model_snapshot().cell(0, 0), field),
                        expected,
                    )

        resets = {
            22: ("bold", "faint"),
            24: ("underline",),
            27: ("inverse",),
            28: ("conceal",),
            29: ("strike",),
            55: ("overline",),
        }
        for code, fields in resets.items():
            with self.subTest(code=code):
                with Shitty(columns=4, rows=2) as terminal:
                    terminal.write(
                        b"\x1b[1;2;4;7;8;9;53m"
                        + f"\x1b[{code}mX".encode()
                    )
                    cell = terminal.model_snapshot().cell(0, 0)
                    for field in fields:
                        self.assertFalse(getattr(cell, field))

    def test_graphics_single_colors(self):
        for code, field, expected in (
            *((30 + index, "foreground_index", index) for index in range(8)),
            *((40 + index, "background_index", index) for index in range(8)),
            *((90 + index, "foreground_index", index + 8) for index in range(8)),
            *((100 + index, "background_index", index + 8) for index in range(8)),
        ):
            with self.subTest(code=code):
                with Shitty(columns=4, rows=2) as terminal:
                    terminal.write(f"\x1b[{code}mX".encode())
                    self.assertEqual(
                        getattr(terminal.model_snapshot().cell(0, 0), field),
                        expected,
                    )

        with Shitty(columns=4, rows=2) as terminal:
            terminal.write(b"\x1b[31;42mA\x1b[39;49mB")
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.cell(1, 0).foreground_index, -2)
            self.assertEqual(snapshot.cell(1, 0).background_index, -2)

    def test_graphics_single_with_subparameters(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(
                b"\x1b[4:3mA"
                b"\x1b[0;38:5:1mB"
                b"\x1b[0;48:5:15mC"
                b"\x1b[0;4;58:5:1mD"
            )
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.cell(0, 0).underline_style, 3)
            self.assertEqual(snapshot.cell(1, 0).foreground_index, 1)
            self.assertEqual(snapshot.cell(2, 0).background_index, 15)
            self.assertEqual(snapshot.cell(3, 0).underline_index, 1)

    def test_graphics_push_pop(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(
                b"\x1b[31m\x1b[#{"
                b"\x1b[32mA"
                b"\x1b[#}B"
                b"\x1b[#{\x1b[34m\x1b[#{\x1b[35mC"
                b"\x1b[#}D\x1b[#}E"
            )
            snapshot = terminal.model_snapshot()
            self.assertEqual(snapshot.cell(0, 0).foreground_index, 2)
            self.assertEqual(snapshot.cell(1, 0).foreground_index, 1)
            self.assertEqual(snapshot.cell(2, 0).foreground_index, 5)
            self.assertEqual(snapshot.cell(3, 0).foreground_index, 4)
            self.assertEqual(snapshot.cell(4, 0).foreground_index, 1)

    def test_graphics_push_pop_selected_attributes(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(
                b"\x1b[1;32;44m"
                b"\x1b[1;31;21#{"
                b"\x1b[22;21;31;42m"
                b"\x1b[#}A"
                b"\x1b[0m\x1b[4#{\x1b[4m\x1b[#}B"
                b"\x1b[0m\x1b[4#{\x1b[21m\x1b[#}C"
                b"\x1b[0;4:3m\x1b[4#{\x1b[21m\x1b[#}D"
            )
            snapshot = terminal.model_snapshot()

            selected = snapshot.cell(0, 0)
            self.assertTrue(selected.bold)
            self.assertEqual(selected.foreground_index, 9)
            self.assertEqual(selected.background_index, 4)
            self.assertEqual(selected.underline_style, 0)
            self.assertEqual(snapshot.cell(1, 0).underline_style, 0)
            self.assertEqual(snapshot.cell(2, 0).underline_style, 2)
            self.assertEqual(snapshot.cell(3, 0).underline_style, 3)

    def test_graphics_persist_brightness(self):
        with Shitty(columns=10, rows=2) as terminal:
            terminal.write(
                b"\x1b[34;1;32mA"
                b"\x1b[0;94mB\x1b[34mC"
                b"\x1b[0;34;1;94mD\x1b[34mE\x1b[32mF"
            )
            snapshot = terminal.model_snapshot()
            expected = (
                (10, True),
                (12, False),
                (4, False),
                (12, True),
                (12, True),
                (10, True),
            )
            for column, (foreground, bold) in enumerate(expected):
                cell = snapshot.cell(column, 0)
                self.assertEqual(cell.foreground_index, foreground)
                self.assertEqual(cell.bold, bold)


class WindowsTerminalAdapterStatusTest(unittest.TestCase):
    def request_setting(self, terminal, setting):
        terminal.write(b"\x1bP$q" + setting + b"\x1b\\")
        return terminal.read_input()

    def test_operating_status(self):
        with Shitty(columns=10, rows=6) as terminal:
            terminal.write(b"\x1b[5n")
            self.assertEqual(terminal.read_input(), b"\x1b[0n")

    def test_cursor_position_report(self):
        with Shitty(columns=10, rows=6) as terminal:
            terminal.write(b"\x1b[3;5H\x1b[6n\x1b[B\x1b[C\x1b[6n")
            self.assertEqual(
                terminal.read_input(),
                b"\x1b[3;5R\x1b[4;6R",
            )

    def test_extended_cursor_position_report(self):
        with Shitty(columns=10, rows=6) as terminal:
            terminal.write(b"\x1b[3;5H\x1b[?6n")
            self.assertEqual(terminal.read_input(), b"\x1b[?3;5;1R")

            # Shitty, like xterm and VTE, exposes one terminal page.
            terminal.write(b"\x1b[3 P\x1b[?6n")
            self.assertEqual(terminal.read_input(), b"\x1b[?3;5;1R")

    def test_private_status_reports(self):
        with Shitty(columns=10, rows=6) as terminal:
            terminal.write(
                b"\x1b[?15n"
                b"\x1b[?25n"
                b"\x1b[?26n"
                b"\x1b[?55n"
                b"\x1b[?56n"
                b"\x1b[?75n"
                b"\x1b[?85n"
            )
            self.assertEqual(
                terminal.read_input(),
                b"\x1b[?13n"
                b"\x1b[?20n"
                b"\x1b[?27;1;0;0n"
                b"\x1b[?50n"
                b"\x1b[?57;1n"
                b"\x1b[?70n"
                b"\x1b[?83n",
            )

    def test_primary_device_attributes(self):
        with Shitty(columns=10, rows=6) as terminal:
            terminal.write(b"\x1b[c")
            self.assertEqual(
                terminal.read_input(),
                b"\x1b[?64;1;2;6;8;9;15;21;22;28;29c",
            )

    def test_secondary_device_attributes(self):
        with Shitty(columns=10, rows=6) as terminal:
            terminal.write(b"\x1b[>c")
            self.assertEqual(terminal.read_input(), b"\x1b[>41;14;0c")

    def test_tertiary_device_attributes(self):
        with Shitty(columns=10, rows=6) as terminal:
            terminal.write(b"\x1b[=c")
            self.assertEqual(
                terminal.read_input(),
                b"\x1bP!|00000000\x1b\\",
            )

    def test_request_displayed_extent(self):
        with Shitty(columns=10, rows=6) as terminal:
            terminal.write(b"\x1b[\"v")
            self.assertEqual(
                terminal.read_input(),
                b"\x1b[6;10;1;1;1\"w",
            )

    def test_request_terminal_parameters(self):
        with Shitty(columns=10, rows=6) as terminal:
            terminal.write(b"\x1b[0x\x1b[1x\x1b[2x")
            self.assertEqual(
                terminal.read_input(),
                b"\x1b[2;1;1;128;128;1;0x"
                b"\x1b[3;1;1;128;128;1;0x",
            )

    def test_request_standard_modes(self):
        for mode in (4, 20):
            with self.subTest(mode=mode):
                with Shitty(columns=10, rows=6) as terminal:
                    terminal.write(
                        f"\x1b[{mode}h\x1b[{mode}$p"
                        f"\x1b[{mode}l\x1b[{mode}$p".encode()
                    )
                    self.assertEqual(
                        terminal.read_input(),
                        f"\x1b[{mode};1$y\x1b[{mode};2$y".encode(),
                    )

    def test_request_private_modes(self):
        supported = (
            1,
            3,
            5,
            6,
            7,
            8,
            12,
            25,
            40,
            66,
            67,
            69,
            1000,
            1002,
            1003,
            1004,
            1005,
            1006,
            1007,
            1049,
            2004,
        )
        for mode in supported:
            with self.subTest(mode=mode):
                with Shitty(columns=10, rows=6) as terminal:
                    allow = b"\x1b[?40h" if mode == 3 else b""
                    terminal.write(
                        allow
                        + f"\x1b[?{mode}h\x1b[?{mode}$p"
                        f"\x1b[?{mode}l\x1b[?{mode}$p".encode()
                    )
                    self.assertEqual(
                        terminal.read_input(),
                        f"\x1b[?{mode};1$y\x1b[?{mode};2$y".encode(),
                    )

        for mode in (117, 9001):
            with self.subTest(unsupported=mode):
                with Shitty(columns=10, rows=6) as terminal:
                    terminal.write(
                        f"\x1b[?{mode}h\x1b[?{mode}$p"
                        f"\x1b[?{mode}l\x1b[?{mode}$p".encode()
                    )
                    self.assertEqual(
                        terminal.read_input(),
                        f"\x1b[?{mode};0$y\x1b[?{mode};0$y".encode(),
                    )

    def test_request_permanent_grapheme_mode(self):
        with Shitty(columns=10, rows=6) as terminal:
            terminal.write(b"\x1b[?2027l\x1b[?2027$p")
            self.assertEqual(
                terminal.read_input(),
                b"\x1b[?2027;3$y",
            )

    def test_request_scrolling_margins(self):
        with Shitty(columns=10, rows=6) as terminal:
            terminal.write(b"\x1b[2;5r")
            self.assertEqual(
                self.request_setting(terminal, b"r"),
                b"\x1bP1$r2;5r\x1b\\",
            )
            terminal.write(b"\x1b[r")
            self.assertEqual(
                self.request_setting(terminal, b"r"),
                b"\x1bP1$r1;6r\x1b\\",
            )

            terminal.write(b"\x1b[?69h\x1b[3;8s")
            self.assertEqual(
                self.request_setting(terminal, b"s"),
                b"\x1bP1$r3;8s\x1b\\",
            )
            terminal.write(b"\x1b[s")
            self.assertEqual(
                self.request_setting(terminal, b"s"),
                b"\x1bP1$r1;10s\x1b\\",
            )

    def test_request_sgr_settings(self):
        cases = (
            (b"\x1b[0m", b"0m"),
            (b"\x1b[0;1;4;7m", b"0;1;4;7m"),
            (b"\x1b[0;4:3m", b"0;4:3m"),
            (b"\x1b[0;2;5;8m", b"0;2;5;8m"),
            (b"\x1b[0;3;9m", b"0;3;9m"),
            (b"\x1b[0;21;53m", b"0;4:2;53m"),
            (b"\x1b[0;33;46m", b"0;33;46m"),
            (b"\x1b[0;96;103m", b"0;96;103m"),
            (
                b"\x1b[0;38:5:123;48:5:45;58:5:128m",
                b"0;38:5:123;48:5:45;58:5:128m",
            ),
            (
                b"\x1b[0;38:2::12:34:56;48:2::65:43:21;"
                b"58:2::128:222:45m",
                b"0;38:2::12:34:56;48:2::65:43:21;"
                b"58:2::128:222:45m",
            ),
        )
        for setup, expected in cases:
            with self.subTest(setup=setup):
                with Shitty(columns=10, rows=6) as terminal:
                    terminal.write(setup)
                    self.assertEqual(
                        self.request_setting(terminal, b"m"),
                        b"\x1bP1$r" + expected + b"\x1b\\",
                    )

    def test_request_cursor_and_protection_settings(self):
        for style in range(7):
            with self.subTest(style=style):
                with Shitty(columns=10, rows=6) as terminal:
                    terminal.write(f"\x1b[{style} q".encode())
                    reported = style if style else 1
                    self.assertEqual(
                        self.request_setting(terminal, b" q"),
                        b"\x1bP1$r"
                        + f"{reported} q".encode()
                        + b"\x1b\\",
                    )

        for protection in (0, 1):
            with self.subTest(protection=protection):
                with Shitty(columns=10, rows=6) as terminal:
                    terminal.write(f"\x1b[{protection}\"q".encode())
                    self.assertEqual(
                        self.request_setting(terminal, b"\"q"),
                        b"\x1bP1$r"
                        + f"{protection}\"q".encode()
                        + b"\x1b\\",
                    )

    def test_request_unsupported_settings(self):
        with Shitty(columns=10, rows=6) as terminal:
            for setting in (b",|", b"1,|", b"2,|", b"3,|", b"x"):
                with self.subTest(setting=setting):
                    self.assertEqual(
                        self.request_setting(terminal, setting),
                        b"\x1bP0$r\x1b\\",
                    )


if __name__ == "__main__":
    unittest.main()
