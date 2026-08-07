#!/usr/bin/env python3

import os
import sys
from itertools import islice
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Shitty
from catalog import category_cases


def main():
    if len(sys.argv) != 6:
        raise SystemExit(
            "usage: adapter.py CATEGORY START END XFAIL_FILE STAMP"
        )
    category = sys.argv[1]
    start, end = map(int, sys.argv[2:4])
    xfail_path = Path(sys.argv[4])
    stamp = Path(sys.argv[5])
    cases = list(islice(category_cases(category), start, end))
    known_failures = {
        line.strip() for line in xfail_path.read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }
    identifiers = {identifier for identifier, _, _ in cases}
    concise = bool(os.environ.get("UCS_DETECT_CONCISE"))

    # The corpus asserts the full current tables; the auto default would
    # follow whatever libc this host happens to run.
    with Shitty(
        columns=80, rows=2, save_lines=0,
        extra_arguments=("-unicodeWidths", "17"),
    ) as terminal:
        positions = terminal.measure_widths(
            *(payload for _, _, payload in cases)
        )

    failures = {}
    for (identifier, expected, _), (column, row) in zip(cases, positions):
        if (column, row) != (expected, 0):
            failures[identifier] = (
                f"expected cursor (column={expected}, row=0), "
                f"got (column={column}, row={row})"
            )

    unexpected = failures.keys() - known_failures
    xpasses = (known_failures & identifiers) - failures.keys()
    expected_failures = known_failures & failures.keys()
    if expected_failures and not concise:
        print(
            f"XFAIL ucs-detect/{category}[{start}:{end}] "
            f"({len(expected_failures)} known mismatches)"
        )
    for identifier in sorted(unexpected):
        if concise:
            print(f"FAIL_ID {identifier}")
        else:
            print(
                f"FAIL ucs-detect/{identifier}: {failures[identifier]}",
                file=sys.stderr,
            )
    for identifier in sorted(xpasses):
        if concise:
            print(f"XPASS_ID {identifier}")
        else:
            print(f"XPASS ucs-detect/{identifier}", file=sys.stderr)
    if unexpected or xpasses:
        return 1

    print(f"PASS ucs-detect/{category}[{start}:{end}] ({len(cases)} cases)")
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
