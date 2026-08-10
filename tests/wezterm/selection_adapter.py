#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Shitty
from selection_cases import case_data


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: selection_adapter.py CASE XFAIL_FILE STAMP")
    name = sys.argv[1]
    known = {
        line.strip() for line in Path(sys.argv[2]).read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }
    stamp = Path(sys.argv[3])
    label, rows, columns, save_lines, operation, expected = case_data(name)
    with Shitty(columns=columns, rows=rows, save_lines=save_lines) as terminal:
        actual = operation(terminal)
    mismatch = actual != expected
    if mismatch != (name in known):
        status = "FAIL" if mismatch else "XPASS"
        print(
            f"{status} WezTerm selection/{name} ({label}): "
            f"expected {expected!r}, got {actual!r}",
            file=sys.stderr,
        )
        return 1
    status = "XFAIL" if mismatch else "PASS"
    print(
        f"{status} WezTerm selection/{name} ({label})"
        + (f": expected {expected!r}, got {actual!r}" if mismatch else "")
    )
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
