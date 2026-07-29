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
    if len(sys.argv) != 3:
        raise SystemExit("usage: semantic_adapter.py CASE STAMP")
    name = sys.argv[1]
    stamp = Path(sys.argv[2])
    label, rows, columns, payload, expected, attributes = case_data(name)
    with Shitty(columns=columns, rows=rows, save_lines=0) as terminal:
        terminal.write(payload)
        snapshot = terminal.snapshot()
    actual = semantic_zones(snapshot)
    if actual != expected:
        raise AssertionError(
            f"WezTerm semantic/{name} ({label}): "
            f"expected={expected}, got={actual}"
        )
    if attributes:
        verify_attributes(snapshot, expected)
    print(
        f"PASS WezTerm semantic/{name} ({label}): "
        f"{len(expected)} zones"
    )
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
