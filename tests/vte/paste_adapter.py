#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Shitty
from paste_cases import (
    C0_CONTROLS,
    C1_CONTROLS,
    CASE_NAMES,
    STRING_CASES,
    control_picture,
    control_vectors,
)


def paste(terminal, content, chunk):
    terminal.set_system_clipboard(content)
    terminal.set_clipboard_chunk(chunk)
    terminal.frontend_key_event(ord("V"), action=1, modifiers=3)
    terminal.frontend_key_event(ord("V"), action=0, modifiers=3)
    return terminal.read_input()


def assert_paste(terminal, content, expected, idempotent=True):
    for chunk in (0, 1):
        actual = paste(terminal, content, chunk)
        if actual != expected:
            raise AssertionError(
                f"{content!r}, chunk {chunk}: "
                f"expected {expected!r}, got {actual!r}"
            )
    if idempotent:
        actual = paste(terminal, expected, 1)
        if actual != expected:
            raise AssertionError(
                f"idempotence for {content!r}: "
                f"expected {expected!r}, got {actual!r}"
            )


def run_brackets_c0():
    with Shitty() as terminal:
        terminal.write(b"\x1b[?2004h")
        assert_paste(
            terminal,
            b"0",
            b"\x1b[200~0\x1b[201~",
            idempotent=False,
        )


def run_controls(controls):
    with Shitty() as terminal:
        for value in controls:
            control = (
                bytes((0xc2, value))
                if value >= 0x80
                else bytes((value,))
            )
            for content, expected in control_vectors(
                control,
                control_picture(value),
            ):
                assert_paste(terminal, content, expected)


def run_strings():
    with Shitty() as terminal:
        for content, expected in STRING_CASES:
            assert_paste(terminal, content, expected)


RUNNERS = {
    "brackets-c0": run_brackets_c0,
    "controls-c0": lambda: run_controls(C0_CONTROLS),
    "controls-c1": lambda: run_controls(C1_CONTROLS),
    "strings": run_strings,
}


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: paste_adapter.py CASE STAMP")
    name = sys.argv[1]
    stamp = Path(sys.argv[2])
    if name not in CASE_NAMES:
        raise SystemExit(f"unknown VTE paste case: {name}")
    RUNNERS[name]()
    print(f"PASS VTE paste/{name}")
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
