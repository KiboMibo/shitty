#!/usr/bin/env python3

import json
import re
import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Shitty


def main():
    if len(sys.argv) != 5:
        raise SystemExit("usage: adapter.py HELPER CASE XFAIL_FILE STAMP")
    helper, case_id = sys.argv[1:3]
    xfail_path = Path(sys.argv[3])
    stamp = Path(sys.argv[4])
    root = Path(__file__).resolve().parent
    cases = {
        identifier: (label, expected)
        for identifier, label, expected in json.loads(
            (root / "cases.json").read_text()
        )
    }
    label, expected = cases[case_id]
    known_failures = {
        line.strip() for line in xfail_path.read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }

    with Shitty(columns=80, rows=60, save_lines=100) as terminal:
        terminal.spawn(helper)
        status, screen = terminal.wait_child(timeout=10)
    if status != 0:
        print(f"ERROR wraptest/{case_id}: helper exited {status}", file=sys.stderr)
        return 1

    flat_screen = "".join(screen.splitlines())
    match = re.search(re.escape(label) + r"\s+(yes|no|-)\s", flat_screen)
    actual = match.group(1) if match else None
    mismatch = None
    if actual is None:
        mismatch = f"missing report row {label!r}; screen was:\n{screen}"
    elif (actual == "yes") != expected:
        mismatch = (
            f"DEC expects {'yes' if expected else 'no'}, helper reported {actual}"
        )

    if case_id in known_failures:
        if mismatch is None:
            print(f"XPASS wraptest/{case_id}", file=sys.stderr)
            return 1
        print(f"XFAIL wraptest/{case_id}: {mismatch}")
    elif mismatch is not None:
        print(f"FAIL wraptest/{case_id}: {mismatch}", file=sys.stderr)
        return 1
    else:
        print(f"PASS wraptest/{case_id}")
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
