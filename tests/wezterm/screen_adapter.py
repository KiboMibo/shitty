#!/usr/bin/env python3

import os
import signal
import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Zutty
from screen_catalog import case_data


def visible_lines(snapshot):
    lines = []
    for row in range(snapshot.rows):
        cells = snapshot.cells[row * snapshot.columns:(row + 1) * snapshot.columns]
        lines.append("".join(cell.char for cell in cells
                             if not cell.double_width_continuation).rstrip())
    return tuple(lines)


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: screen_adapter.py CASE XFAIL_FILE STAMP")
    name = sys.argv[1]
    known = {
        line.strip() for line in Path(sys.argv[2]).read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }
    timeout_message = f"FAIL WezTerm-screen/{name}: exceeded 10 seconds\n".encode()

    def timed_out(_signum, _frame):
        os.write(2, timeout_message)
        os._exit(124)

    signal.signal(signal.SIGALRM, timed_out)
    signal.alarm(10)
    label, rows, columns, save_lines, payload, expected = case_data(name)
    with Zutty(columns=columns, rows=rows, save_lines=save_lines) as terminal:
        terminal.write(payload)
        actual = visible_lines(terminal.snapshot())
    normalized_expected = tuple(line.rstrip() for line in expected)
    mismatch = None if actual == normalized_expected else (
        f"screen got {actual!r}, expected {normalized_expected!r}")
    signal.alarm(0)
    if (mismatch is not None) != (name in known):
        status = "FAIL" if mismatch is not None else "XPASS"
        print(f"{status} WezTerm-screen/{name} ({label}): "
              f"{mismatch or 'matched'}", file=sys.stderr)
        return 1
    print(f"{'XFAIL' if mismatch else 'PASS'} WezTerm-screen/{name} ({label})"
          + (f": {mismatch}" if mismatch else ""))
    stamp = Path(sys.argv[3])
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
