#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Shitty
from utf8_cases import case_names, replacement_vectors


def valid_scalars(begin, end):
    return tuple(
        codepoint
        for codepoint in range(begin, end)
        if not 0xD800 <= codepoint <= 0xDFFF
    )


def run_decode():
    checked = 0
    batch_size = 4096
    with Shitty(columns=5, rows=2, save_lines=0) as terminal:
        for begin in range(0, 0x110000, batch_size):
            expected = valid_scalars(begin, min(begin + batch_size, 0x110000))
            payload = b"".join(chr(codepoint).encode() for codepoint in expected)
            actual = terminal.utf8_push(payload)
            if actual != expected:
                limit = min(len(actual), len(expected))
                index = next(
                    (
                        offset
                        for offset in range(limit)
                        if actual[offset] != expected[offset]
                    ),
                    limit,
                )
                raise AssertionError(
                    f"scalar batch {begin:#x}: mismatch at {index}, "
                    f"got={actual[index:index + 4]}, "
                    f"expected={expected[index:index + 4]}"
                )
            checked += len(expected)
        tail = terminal.utf8_flush()
        if tail:
            raise AssertionError(f"complete scalar stream left tail {tail!r}")
    print(f"PASS VTE utf8/decode: {checked} scalar values")


def run_replacement():
    vectors = replacement_vectors()
    with Shitty(columns=5, rows=2, save_lines=0) as terminal:
        for index, (payload, expected) in enumerate(vectors):
            terminal.utf8_reset()
            actual = terminal.utf8_push(payload) + terminal.utf8_flush()
            if actual != expected:
                raise AssertionError(
                    f"replacement vector {index}: input={payload.hex()}, "
                    f"got={actual}, expected={expected}"
                )
    print(f"PASS VTE utf8/replacement: {len(vectors)} vectors")


RUNNERS = {
    "decode": run_decode,
    "replacement": run_replacement,
}


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: utf8_adapter.py CASE STAMP")
    name = sys.argv[1]
    stamp = Path(sys.argv[2])
    if name not in case_names():
        raise SystemExit(f"unknown VTE UTF-8 case: {name}")
    RUNNERS[name]()
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
