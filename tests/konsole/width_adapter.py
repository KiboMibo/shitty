#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Shitty
from width_catalog import case_vector


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: width_adapter.py CASE STAMP")
    name = sys.argv[1]
    stamp = Path(sys.argv[2])
    label, codepoint, expected = case_vector(name)
    with Shitty(columns=4, rows=2, save_lines=0) as terminal:
        actual, = terminal.codepoint_widths(codepoint)
    if actual != expected:
        raise AssertionError(
            f"Konsole CharacterWidth/{label}: U+{codepoint:04X} "
            f"expected {expected}, got {actual}"
        )
    print(
        f"PASS Konsole CharacterWidth/{label}: "
        f"U+{codepoint:04X} width {actual}"
    )
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
