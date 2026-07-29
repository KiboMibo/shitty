#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from damage_cases import case_data
from harness import Shitty


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: damage_adapter.py CASE STAMP")
    name = sys.argv[1]
    stamp = Path(sys.argv[2])
    (
        label,
        rows,
        columns,
        save_lines,
        actions,
        upstream_rows,
        expected,
    ) = case_data(name)
    with Shitty(columns=columns, rows=rows, save_lines=save_lines) as terminal:
        for action in actions:
            if action[0] != "write":
                raise ValueError(f"unknown damage action {action[0]}")
            terminal.write(action[1])
        actual = terminal.last_update_rows()
    if actual != expected:
        raise AssertionError(
            f"WezTerm damage/{name} ({label}): "
            f"upstream stable rows={upstream_rows}, "
            f"expected visible rows={expected}, got {actual}"
        )
    print(
        f"PASS WezTerm damage/{name} ({label}): "
        f"stable={upstream_rows}, visible={expected}"
    )
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
