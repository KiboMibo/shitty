#!/usr/bin/env python3

import os
import signal
import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Shitty
from known_cases import CASES


def summarize(events):
    return [(event, data[:80], len(data)) for event, data in events]


def exercise(cases, bytewise):
    payload = b"".join(case.sequence for case in cases)
    with Shitty(columns=5, rows=5, save_lines=5) as terminal:
        terminal.parser_trace_on()
        if bytewise:
            terminal.write_chunks(*(payload[index:index + 1]
                                    for index in range(len(payload))))
        else:
            terminal.write(payload)
        return terminal.parser_trace()


def difference(actual, expected):
    if actual == expected:
        return None
    limit = min(len(actual), len(expected))
    index = next(
        (position for position in range(limit)
         if actual[position] != expected[position]),
        limit,
    )
    return (
        f"event {index}: got {summarize(actual[index:index + 3])!r}, "
        f"expected {summarize(expected[index:index + 3])!r}; "
        f"totals {len(actual)}/{len(expected)}"
    )


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: known_adapter.py CASE STAMP")
    kind = sys.argv[1]
    cases = CASES[kind]
    timeout_message = f"FAIL VTE/known/{kind}: exceeded 30 seconds\n".encode()

    def timed_out(_signum, _frame):
        os.write(2, timeout_message)
        os._exit(124)

    signal.signal(signal.SIGALRM, timed_out)
    signal.alarm(30)
    expected = [case.event for case in cases]
    mismatch = difference(exercise(cases, False), expected)
    if mismatch is None:
        mismatch = difference(exercise(cases, True), expected)
    signal.alarm(0)
    if mismatch is not None:
        print(f"FAIL VTE/known/{kind}: {mismatch}", file=sys.stderr)
        return 1
    print(
        f"PASS VTE/known/{kind}: {len(cases)} commands, "
        f"{sum(case.nop for case in cases)} NOP"
    )
    stamp = Path(sys.argv[2])
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
