#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Shitty
from semantic_cases import case_data


SEMANTIC = {
    0: "Output",
    1: "Prompt",
    2: "Input",
    3: "Output",
}


def semantic_zones(snapshot):
    zones = []
    for row in range(snapshot.rows):
        for column in range(snapshot.columns):
            cell = snapshot.cell(column, row)
            if not cell.drawn:
                continue
            semantic = SEMANTIC[cell.semantic]
            if zones and zones[-1][-1] == semantic:
                zones[-1] = (
                    zones[-1][0],
                    zones[-1][1],
                    row,
                    column,
                    semantic,
                )
            else:
                zones.append((row, column, row, column, semantic))
    return tuple(zones)


def verify_attributes(snapshot, expected):
    for start_row, start_column, end_row, end_column, semantic in expected:
        begin = start_row * snapshot.columns + start_column
        end = end_row * snapshot.columns + end_column
        for index in range(begin, end + 1):
            cell = snapshot.cells[index]
            if not cell.drawn:
                continue
            actual = SEMANTIC[cell.semantic]
            if actual != semantic:
                raise AssertionError(
                    f"cell {index} expected semantic {semantic}, got {actual}"
                )


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: semantic_adapter.py CASE XFAIL_FILE STAMP")
    name = sys.argv[1]
    known = {
        line.strip() for line in Path(sys.argv[2]).read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }
    stamp = Path(sys.argv[3])
    label, rows, columns, payload, expected, attributes = case_data(name)
    with Shitty(columns=columns, rows=rows, save_lines=0) as terminal:
        terminal.write(payload)
        snapshot = terminal.snapshot()
    actual = semantic_zones(snapshot)
    mismatch = None if actual == expected else (
        f"expected={expected}, got={actual}")
    if (mismatch is not None) != (name in known):
        status = "FAIL" if mismatch is not None else "XPASS"
        print(
            f"{status} WezTerm semantic/{name} ({label}): "
            f"{mismatch or 'matched'}",
            file=sys.stderr,
        )
        return 1
    if mismatch is None and attributes:
        verify_attributes(snapshot, expected)
    print(
        f"{'XFAIL' if mismatch else 'PASS'} WezTerm semantic/{name} ({label}): "
        f"{len(expected)} zones"
    )
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
