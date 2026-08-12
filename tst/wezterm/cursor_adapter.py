#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from cursor_cases import case_data
from harness import Shitty


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: cursor_adapter.py CASE STAMP")
    name = sys.argv[1]
    stamp = Path(sys.argv[2])
    label, rows, columns, save_lines, actions, expected, pending = case_data(name)
    with Shitty(columns=columns, rows=rows, save_lines=save_lines) as terminal:
        for action in actions:
            if action[0] == "write":
                terminal.write(action[1])
            elif action[0] == "resize":
                terminal.resize(action[1], action[2])
            else:
                raise ValueError(f"unknown cursor action {action[0]}")
        snapshot = terminal.snapshot()
        actual = snapshot.cursor_x, snapshot.cursor_y
        visible, _blink, style = terminal.cursor_state()
        actual_pending = terminal.cursor_pending_wrap()
    if actual != expected:
        raise AssertionError(
            f"WezTerm cursor/{name} ({label}): "
            f"expected {expected}, got {actual}"
        )
    if not visible or style != 1:
        raise AssertionError(
            f"WezTerm cursor/{name} ({label}): "
            f"expected visible default cursor, got visible={visible}, style={style}"
        )
    if pending is not None and actual_pending != pending:
        raise AssertionError(
            f"WezTerm cursor/{name} ({label}): "
            f"expected pending-wrap={pending}, got {actual_pending}"
        )
    print(f"PASS WezTerm cursor/{name} ({label})")
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
