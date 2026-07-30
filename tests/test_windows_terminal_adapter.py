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
}


def upstream_methods():
    return set(re.findall(r"TEST_METHOD\((\w+)\)", UPSTREAM.read_text()))


class WindowsTerminalAdapterCursorTest(unittest.TestCase):
    def test_upstream_inventory_has_53_methods(self):
        methods = upstream_methods()
        self.assertEqual(len(methods), 53)
        self.assertLessEqual(PORTED_METHODS, methods)

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


if __name__ == "__main__":
    unittest.main()
