#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import re
import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Shitty
from keyboard_catalog import case_vector


F12 = 301
PRESS = 1
MODIFIED_F12 = re.compile(rb"\x1b\[24;([2-8])~")


def modifier_parameter(output):
    if output == b"\x1b[24~":
        return 1
    match = MODIFIED_F12.fullmatch(output)
    if match is None:
        raise AssertionError(f"invalid F12 sequence: {output!r}")
    return int(match.group(1))


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: keyboard_adapter.py CASE STAMP")
    name = sys.argv[1]
    stamp = Path(sys.argv[2])
    label, text, expected, wildcards, modifiers = case_vector(name)
    with Shitty(columns=4, rows=2, save_lines=0) as terminal:
        terminal.frontend_key_event(F12, PRESS, modifiers=modifiers)
        parameter = modifier_parameter(terminal.read_input())
    actual = text.replace(b"*", str(parameter).encode()) if wildcards else text
    if actual != expected:
        raise AssertionError(
            f"Konsole KeyboardTranslator/{label!r}: "
            f"expected {expected!r}, got {actual!r}; "
            f"modifier parameter={parameter}"
        )
    print(
        f"PASS Konsole KeyboardTranslator/{label!r}: "
        f"modifier parameter {parameter}"
    )
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
