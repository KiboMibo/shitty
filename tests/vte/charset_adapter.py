#!/usr/bin/env python3

import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Shitty
from vte.charset_cases import CASE_NAMES, PRESEED, cases


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: charset_adapter.py CASE STAMP")
    name = sys.argv[1]
    if name not in CASE_NAMES:
        raise SystemExit(f"unknown VTE charset case: {name}")

    checked = 0
    with Shitty(columns=5, rows=5, save_lines=5) as terminal:
        for sequence, expected in cases(name):
            terminal.write(PRESEED + sequence)
            actual = terminal.charset_state()
            if actual != expected:
                raise AssertionError(
                    f"{name} case {checked}, {sequence!r}: "
                    f"got {actual!r}, expected {expected!r}"
                )
            checked += 1

    stamp = Path(sys.argv[2])
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    print(f"PASS VTE charset/{name}: {checked} designators")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
