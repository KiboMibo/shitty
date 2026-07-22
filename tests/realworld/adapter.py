#!/usr/bin/env python3

import difflib
import json
import signal
import sys
from compression import zstd
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(TESTS))

from harness import Shitty
from corpus import (
    canonical_snapshot, encode_snapshot, read_cases,
    verify_snapshot_contract,
)


CHUNK_SIZES = (1, 7, 31, 127, 509, 2039, 8191, 32749, 65521)


def replay(terminal, data):
    offset = 0
    chunk = 0
    while offset < len(data):
        size = CHUNK_SIZES[chunk % len(CHUNK_SIZES)]
        terminal.write(data[offset:offset + size])
        offset += size
        chunk += 1


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: adapter.py CASE STAMP")
    case_name = sys.argv[1]
    stamp = Path(sys.argv[2])
    cases = read_cases(ROOT / "cases.json")
    if case_name not in cases:
        raise SystemExit(f"unknown real-world case: {case_name}")
    case = cases[case_name]

    def timed_out(_signum, _frame):
        raise TimeoutError(f"real-world/{case_name} exceeded 15 seconds")

    signal.signal(signal.SIGALRM, timed_out)
    signal.alarm(15)
    compressed = (ROOT / "input" / f"{case_name}.input.zst").read_bytes()
    data = zstd.decompress(compressed)
    with Shitty(
        columns=case["columns"],
        rows=case["rows"],
        save_lines=case["save_lines"],
        extra_arguments=case.get("shitty_arguments", ()),
    ) as terminal:
        replay(terminal, data)
        actual = canonical_snapshot(
            terminal.model_snapshot(), terminal.render_state())
        verify_snapshot_contract(
            actual,
            case.get("expected_text", ()),
            case.get("minimum_color_styles", 0),
        )
    signal.alarm(0)

    expected_path = ROOT / "screen" / f"{case_name}.screen.json"
    expected = json.loads(expected_path.read_text())
    if actual != expected:
        actual_text = encode_snapshot(actual)
        expected_text = encode_snapshot(expected)
        actual_path = stamp.with_suffix(".actual.json")
        actual_path.parent.mkdir(parents=True, exist_ok=True)
        actual_path.write_text(actual_text)
        difference = difflib.unified_diff(
            expected_text.splitlines(keepends=True),
            actual_text.splitlines(keepends=True),
            fromfile=str(expected_path),
            tofile=str(actual_path),
        )
        print(f"FAIL real-world/{case_name}", file=sys.stderr)
        print("".join(list(difference)[:300]), file=sys.stderr)
        return 1

    print(f"PASS real-world/{case_name} ({len(data)} PTY bytes)")
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
