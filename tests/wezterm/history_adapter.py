#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Shitty
from history_cases import case_data


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: history_adapter.py CASE STAMP")
    name = sys.argv[1]
    stamp = Path(sys.argv[2])
    (
        label,
        rows,
        columns,
        save_lines,
        actions,
        upstream,
        expected,
        expected_history,
        upstream_stable,
    ) = case_data(name)
    with Shitty(columns=columns, rows=rows, save_lines=save_lines) as terminal:
        for action in actions:
            if action[0] != "write":
                raise ValueError(f"unknown history action {action[0]}")
            terminal.write(action[1])
        actual = terminal.all_text()
        history, _total, _rows, viewport = terminal.scrollback_state()
    if actual != expected:
        raise AssertionError(
            f"WezTerm history/{name} ({label}): "
            f"upstream={upstream!r}, expected={expected!r}, got={actual!r}"
        )
    if history != expected_history or viewport != expected_history:
        raise AssertionError(
            f"WezTerm history/{name} ({label}): expected history/viewport "
            f"{expected_history}, got history={history}, viewport={viewport}; "
            f"upstream stable rows={upstream_stable}"
        )
    print(
        f"PASS WezTerm history/{name} ({label}): "
        f"lines={len(expected)}, history={history}, "
        f"stable={upstream_stable}"
    )
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
