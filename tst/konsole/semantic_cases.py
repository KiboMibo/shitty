#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.


CASE_NAMES = (
    "testLargeScreenCopyShortLine",
    "testLargeScreenCopyEmptyLine",
    "testLargeScreenCopyLongLine",
    "testBlockSelection",
    "testCJKBlockSelection",
    "testCursorPosition",
    "testHistoryNone",
    "testHistoryFile",
    "testCompactHistory",
    "testEmulationHistory",
    "testHistoryScroll",
    "testHistoryReflow",
    "testHistoryTypeChange",
)


def case_names():
    return CASE_NAMES


def expect(actual, expected, subject):
    if actual != expected:
        raise AssertionError(
            f"{subject}: expected {expected!r}, got {actual!r}"
        )


def select(terminal, start, end, rectangular=False):
    terminal.select_start(*start)
    if rectangular:
        terminal.select_rectangular()
    terminal.select_update(*end)
    return terminal.select_finish()


def testLargeScreenCopyShortLine(factory):
    with factory(columns=1200, rows=10) as terminal:
        terminal.write(b"0123456789abcde")
        expect(
            select(terminal, (0, 0), (1200, 0)),
            b"0123456789abcde",
            "large short line",
        )


def testLargeScreenCopyEmptyLine(factory):
    with factory(columns=1200, rows=10) as terminal:
        expect(select(terminal, (0, 0), (1200, 0)), b"", "empty line")


def testLargeScreenCopyLongLine(factory):
    payload = b"0123456789" * 130
    with factory(columns=1200, rows=10) as terminal:
        terminal.write(payload)
        expect(
            select(terminal, (0, 0), (1200, 0)),
            payload[:1200],
            "soft-wrapped large line",
        )


def testBlockSelection(factory):
    payload = b"abcd efgh ijkl mnop qrst uvxz ABCD EFGH IJKL MNOP QRST UVXZ"
    with factory(columns=1200, rows=10) as terminal:
        terminal.write(payload)
        terminal.resize(10, 10)
        expect(
            terminal.model_snapshot().lines[:6],
            [
                "abcd efgh ",
                "ijkl mnop ",
                "qrst uvxz ",
                "ABCD EFGH ",
                "IJKL MNOP ",
                "QRST UVXZ ",
            ],
            "reflowed rows",
        )
        expect(
            select(terminal, (0, 0), (4, 1), True),
            b"abcd\nijkl",
            "rectangular reflow selection",
        )


CJK_TEXT = (
    "챠트 피면 술컵"
    "01234567890123"
    " 도 유효작    "
    "01234567890123"
    "챠트 피면 술컵"
    "01234567890123"
    " 도 유효작    "
    "いろはにほへと"
    "01234567890123"
    " ちりぬるを   "
    "01234567890123"
    "わかよたれそ  "
    "01234567890123"
    " つねならむ   "
    "01234567890123"
    "うゐのおくやま"
    "01234567890123"
    " けふこえて   "
    "01234567890123"
    "あさきゆめみし"
    "01234567890123"
    "ゑひもせす"
)


def testCJKBlockSelection(factory):
    with factory(columns=1200, rows=32) as terminal:
        terminal.write(CJK_TEXT.encode())
        terminal.resize(14, 32)
        expect(
            select(terminal, (2, 0), (7, 15), True),
            (
                "트 피\n23456\n 유효\n23456\n트 피\n23456\n 유효\n"
                "ろはに\n23456\nりぬ\n23456\nかよた\n23456\nねな\n"
                "23456\nゐのお"
            ).encode(),
            "CJK rectangular selection",
        )


def cursor(terminal):
    snapshot = terminal.model_snapshot()
    return snapshot.cursor_x, snapshot.cursor_y


def testCursorPosition(factory):
    with factory(columns=1200, rows=10) as terminal:
        checks = (
            (b"\x1b[6;6H", (5, 5)),
            (b"\x1b[2147483647;2147483647H", (1199, 9)),
            (b"\x1b[0;0H", (0, 0)),
            (b"\x1b[1;1H", (0, 0)),
            (b"\x1b[2147483647B", (0, 9)),
            (b"\x1b[2147483647A", (0, 0)),
            (b"\x1b[4B", (0, 4)),
            (b"\x1b[B", (0, 5)),
            (b"\x1b[0B", (0, 6)),
            (b"\x1b[0A", (0, 5)),
            (b"\x1b[A", (0, 4)),
            (b"\x1b[4A", (0, 0)),
            (b"\x1b[C", (1, 0)),
            (b"\x1b[3C", (4, 0)),
            (b"\x1b[0C", (5, 0)),
            (b"\x1b[0D", (4, 0)),
            (b"\x1b[2D", (2, 0)),
            (b"\x1b[D", (1, 0)),
            (b"\x1b[2147483647C", (1199, 0)),
            (b"\x1b[2147483647D", (0, 0)),
            (b"\x1b[4E", (0, 4)),
            (b"\x1b[E", (0, 5)),
            (b"\x1b[0E", (0, 6)),
            (b"\x1b[0F", (0, 5)),
            (b"\x1b[2F", (0, 3)),
            (b"\x1b[F", (0, 2)),
            (b"\x1b[2147483647F", (0, 0)),
            (b"\x1b[2147483647E", (0, 9)),
        )
        for sequence, expected in checks:
            terminal.write(sequence)
            expect(cursor(terminal), expected, f"cursor after {sequence!r}")


def testHistoryNone(factory):
    with factory(columns=4, rows=2, save_lines=0) as terminal:
        terminal.write(b"A\r\nB\r\nC")
        expect(terminal.scrollback_state(), (0, 2, 2, 0), "disabled history")


def testHistoryFile(factory):
    with factory(columns=4, rows=2, save_lines=100) as terminal:
        terminal.write(b"A\r\nB\r\nC")
        expect(terminal.scrollback_state(), (1, 3, 2, 1), "enabled history")


def testCompactHistory(factory):
    with factory(columns=4, rows=2, save_lines=42) as terminal:
        terminal.write(b"".join(f"{value:02d}\r\n".encode() for value in range(100)))
        state = terminal.scrollback_state()
        expect(state[0], 42, "configured history limit")
        expect(state[1], 44, "history plus visible rows")
        terminal.wheel_up(100)
        expect(
            terminal.model_snapshot().lines,
            ["57  ", "58  "],
            "oldest retained compact rows",
        )


def testEmulationHistory(factory):
    testHistoryNone(factory)
    testHistoryFile(factory)
    testCompactHistory(factory)


def testHistoryScroll(factory):
    for save_lines, expected in ((0, 0), (42, 3), (100, 3)):
        with factory(columns=4, rows=2, save_lines=save_lines) as terminal:
            expect(terminal.scrollback_state()[0], 0, "initial history")
            terminal.write(b"A\r\nB\r\nC\r\nD\r\nE")
            expect(
                terminal.scrollback_state()[0],
                expected,
                f"history size for limit {save_lines}",
            )


def testHistoryReflow(factory):
    payload = b"abcdefghijklmnopqrstuvwxyz1234567890"
    with factory(columns=36, rows=4, save_lines=10) as terminal:
        terminal.write(payload)
        terminal.resize(10, 4)
        expect(
            terminal.model_snapshot().lines,
            ["abcdefghij", "klmnopqrst", "uvwxyz1234", "567890    "],
            "history reflow to width 10",
        )
        terminal.resize(1, 10)
        snapshot = terminal.model_snapshot()
        expect(
            "".join(snapshot.lines[-10:]),
            "1234567890",
            "bounded one-column reflow tail",
        )


def testHistoryTypeChange(factory):
    payload = b"abcdefghijklmnopqrstuvwxyz1234567890"
    with factory(columns=1, rows=2, save_lines=10) as terminal:
        terminal.write(payload)
        terminal.wheel_up(100)
        expect(
            "".join(terminal.model_snapshot().lines),
            "yz",
            "finite history keeps newest tail",
        )
    with factory(columns=1, rows=2, save_lines=0) as terminal:
        terminal.write(payload)
        expect(
            "".join(terminal.model_snapshot().lines),
            "90",
            "disabled history keeps screen only",
        )


CASES = {name: globals()[name] for name in CASE_NAMES}


def run_case(name, factory):
    try:
        case = CASES[name]
    except KeyError as error:
        raise KeyError(f"unknown Konsole semantic case: {name}") from error
    case(factory)
