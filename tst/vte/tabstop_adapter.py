#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Shitty
from tabstop_cases import case_names


def expected_table(columns, stops):
    stop_set = set(stops)
    return tuple(column in stop_set for column in range(columns))


def default_table(columns, width=8):
    return tuple(column % width == 0 for column in range(columns))


def assert_table(terminal, expected, label):
    actual = terminal.tab_stops(len(expected))
    if actual != expected:
        differing = tuple(
            index
            for index, (got, want) in enumerate(zip(actual, expected))
            if got != want
        )
        raise AssertionError(
            f"{label}: {len(differing)} differing tab stops; "
            f"first={differing[:16]}"
        )


def set_stops(terminal, stops):
    payload = bytearray(b"\x1b[3g")
    for column in stops:
        payload.extend(f"\x1b[{column + 1}G\x1bH".encode())
    terminal.write(bytes(payload))


def cursor(terminal, column):
    terminal.write(f"\x1b[{column + 1}G".encode())


def previous(terminal, column, count=1):
    cursor(terminal, column)
    terminal.write(f"\x1b[{count}Z".encode())
    return terminal.snapshot().cursor_x


def next_stop(terminal, column, count=1):
    cursor(terminal, column)
    terminal.write(f"\x1b[{count}I".encode())
    return terminal.snapshot().cursor_x


def assert_queries(operation, queries, label):
    for start, expected in queries:
        actual = operation(start)
        if actual != expected:
            raise AssertionError(
                f"{label}: start={start}, expected={expected}, got={actual}"
            )


def run_default():
    with Shitty(columns=80, rows=2, save_lines=0) as terminal:
        assert_table(terminal, default_table(80), "default")


def run_get_set():
    stops = (42, 200)
    with Shitty(columns=256, rows=2, save_lines=0) as terminal:
        set_stops(terminal, stops)
        assert_table(terminal, expected_table(256, stops), "get-set")


def run_clear():
    with Shitty(columns=128, rows=2, save_lines=0) as terminal:
        terminal.write(b"\x1b[3g")
        assert_table(terminal, expected_table(128, ()), "clear")


def run_reset():
    width = 7
    stops = tuple(range(0, 80, width))
    with Shitty(columns=80, rows=2, save_lines=0) as terminal:
        set_stops(terminal, stops)
        assert_table(terminal, default_table(80, width), "custom width 7")

        terminal.resize(160, 2)
        expected = expected_table(
            160,
            stops + tuple(range(80, 160, 8)),
        )
        assert_table(
            terminal,
            expected,
            "public resize preserves old custom stops and fills new columns",
        )

        terminal.resize(80, 2)
        terminal.write(b"\x1b[3g")
        terminal.resize(160, 2)
        assert_table(
            terminal,
            expected_table(160, range(80, 160, 8)),
            "cleared table grows with default stops only in new columns",
        )

        terminal.resize(256, 2)
        terminal.write(b"\x1bc")
        assert_table(terminal, default_table(256), "RIS reset")
        terminal.resize(1024, 2)
        assert_table(terminal, default_table(1024), "reset grow 1024")
        terminal.resize(4096, 2)
        assert_table(terminal, default_table(4096), "reset grow 4096")


def run_resize():
    with Shitty(columns=80, rows=2, save_lines=0) as terminal:
        assert_table(terminal, default_table(80), "resize initial")
        terminal.resize(161, 2)
        assert_table(
            terminal,
            default_table(161),
            "terminal default table fills newly addressable columns",
        )


def run_previous():
    stops = (0, 31, 32, 63, 64, 255, 256)
    with Shitty(columns=512, rows=2, save_lines=0) as terminal:
        set_stops(terminal, stops)
        assert_queries(
            lambda start: previous(terminal, start),
            ((511, 256), (256, 255), (255, 64), (64, 63),
             (63, 32), (32, 31), (31, 0)),
            "previous/1",
        )
        assert_queries(
            lambda start: previous(terminal, start, 2),
            ((511, 255), (257, 255), (254, 63), (64, 32),
             (33, 31), (32, 0), (31, 0), (0, 0)),
            "previous/2",
        )

        set_stops(terminal, (127, 256))
        assert_queries(
            lambda start: previous(terminal, start),
            ((511, 256), (256, 127), (127, 0),
             (384, 256), (192, 127), (92, 0)),
            "previous/sparse",
        )

        width = 3
        set_stops(terminal, range(0, 512, width))
        for position in range(1, 512):
            actual = previous(terminal, position)
            expected = (position - 1) // width * width
            if actual != expected:
                raise AssertionError(
                    f"previous/default-3: start={position}, "
                    f"expected={expected}, got={actual}"
                )
        if previous(terminal, 0) != 0:
            raise AssertionError("previous/default-3 must clamp npos to column 0")


def run_next():
    stops = (0, 31, 32, 63, 64, 255, 256)
    with Shitty(columns=512, rows=2, save_lines=0) as terminal:
        set_stops(terminal, stops)
        assert_queries(
            lambda start: next_stop(terminal, start),
            ((0, 31), (31, 32), (32, 63), (63, 64),
             (64, 255), (255, 256), (256, 511)),
            "next/1",
        )
        assert_queries(
            lambda start: next_stop(terminal, start, 2),
            ((0, 32), (2, 32), (31, 63), (48, 64),
             (128, 256), (255, 511)),
            "next/2",
        )

        set_stops(terminal, (127, 256))
        assert_queries(
            lambda start: next_stop(terminal, start),
            ((0, 127), (127, 256), (256, 511),
             (1, 127), (192, 256), (384, 511)),
            "next/sparse",
        )

        width = 3
        set_stops(terminal, range(0, 512, width))
        for position in range(0, 509):
            actual = next_stop(terminal, position)
            expected = (position // width + 1) * width
            if actual != expected:
                raise AssertionError(
                    f"next/default-3: start={position}, "
                    f"expected={expected}, got={actual}"
                )
        if next_stop(terminal, 511) != 511:
            raise AssertionError("next/default-3 must clamp npos to last column")


RUNNERS = {
    "default": run_default,
    "get-set": run_get_set,
    "clear": run_clear,
    "reset": run_reset,
    "resize": run_resize,
    "previous": run_previous,
    "next": run_next,
}


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: tabstop_adapter.py CASE STAMP")
    name = sys.argv[1]
    stamp = Path(sys.argv[2])
    if name not in case_names():
        raise SystemExit(f"unknown VTE tabstop case: {name}")
    RUNNERS[name]()
    print(f"PASS VTE tabstops/{name}")
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
