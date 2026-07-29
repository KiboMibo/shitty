#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Shitty
from mode_cases import case_names


def mode_query(mode, private=False):
    prefix = "?" if private else ""
    return f"\x1b[{prefix}{mode}$p".encode()


def mode_reply(mode, state, private=False):
    prefix = "?" if private else ""
    return f"\x1b[{prefix}{mode};{state}$y".encode()


def query(terminal, mode, private=False):
    terminal.write(mode_query(mode, private))
    return terminal.read_input()


def assert_mode(terminal, mode, state, private=False):
    actual = query(terminal, mode, private)
    expected = mode_reply(mode, state, private)
    if actual != expected:
        raise AssertionError(
            f"mode {mode}: expected {expected!r}, got {actual!r}"
        )


def run_ecma():
    with Shitty() as terminal:
        assert_mode(terminal, 4, 2)
        terminal.write(b"\x1b[4h")
        assert_mode(terminal, 4, 1)
        terminal.write(b"\x1bc")
        assert_mode(terminal, 4, 2)


def run_private():
    with Shitty() as terminal:
        assert_mode(terminal, 7, 1, True)
        assert_mode(terminal, 1039, 1, True)
        assert_mode(terminal, 1004, 2, True)

        terminal.write(b"\x1b[?1004h\x1b[?1004s\x1b[?1004l")
        assert_mode(terminal, 1004, 2, True)
        terminal.write(b"\x1b[?1004r")
        assert_mode(terminal, 1004, 1, True)

        terminal.write(b"\x1b[?1004s\x1bc\x1b[?1004r")
        assert_mode(terminal, 1004, 2, True)


RUNNERS = {
    "ecma": run_ecma,
    "private": run_private,
}


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: mode_adapter.py CASE STAMP")
    name = sys.argv[1]
    stamp = Path(sys.argv[2])
    if name not in case_names():
        raise SystemExit(f"unknown VTE mode case: {name}")
    RUNNERS[name]()
    print(f"PASS VTE modes/{name}")
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
