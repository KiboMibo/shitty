#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import shutil
import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Shitty
from pty_catalog import case_names


def test_window_size():
    with Shitty(columns=80, rows=40, glyph_px=8, glyph_py=16) as terminal:
        actual = terminal.winsize_full()
    expected = (80, 40, 640, 640)
    if actual != expected:
        raise AssertionError(
            f"Konsole PtyTest/testWindowSize: expected {expected}, got {actual}"
        )


def test_run_program():
    shell = shutil.which("sh")
    if shell is None:
        raise AssertionError("Konsole PtyTest/testRunProgram: sh was not found")
    with Shitty(columns=4, rows=2, save_lines=0) as terminal:
        terminal.spawn(shell)


CASES = {
    "testWindowSize": test_window_size,
    "testRunProgram": test_run_program,
}


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: pty_adapter.py CASE STAMP")
    name = sys.argv[1]
    stamp = Path(sys.argv[2])
    if name not in case_names():
        raise SystemExit(f"unknown Konsole PtyTest case: {name}")
    CASES[name]()
    print(f"PASS Konsole PtyTest/{name}")
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
