#!/usr/bin/env python3

import os
import re
import signal
import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Shitty
from width_catalog import case_vectors


CPR = re.compile(rb"\x1b\[([0-9]+);([0-9]+)R")


def exercise(vectors):
    stream = bytearray()
    for codepoint, _width in vectors:
        stream.extend(b"\rA")
        stream.extend(chr(codepoint).encode("utf-8"))
        stream.extend(b"\x1b[6n")
    with Shitty(columns=10, rows=2, save_lines=0) as terminal:
        terminal.write(bytes(stream))
        replies = terminal.read_input()
    parsed = [(int(row), int(column)) for row, column in CPR.findall(replies)]
    if len(parsed) != len(vectors):
        return f"received {len(parsed)} CPR replies for {len(vectors)} codepoints"
    for (codepoint, width), (row, column) in zip(vectors, parsed):
        expected = (1, 2 + width)
        if (row, column) != expected:
            return (f"U+{codepoint:04X}: got CPR {row};{column}, expected "
                    f"{expected[0]};{expected[1]} for width {width}")
    return None


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: width_adapter.py CASE XFAIL_FILE STAMP")
    name = sys.argv[1]
    known = {
        line.strip() for line in Path(sys.argv[2]).read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }
    timeout_message = f"FAIL VTE-width/{name}: exceeded 20 seconds\n".encode()

    def timed_out(_signum, _frame):
        os.write(2, timeout_message)
        os._exit(124)

    signal.signal(signal.SIGALRM, timed_out)
    signal.alarm(20)
    vectors = case_vectors(name)
    mismatch = exercise(vectors)
    signal.alarm(0)
    if (mismatch is not None) != (name in known):
        status = "FAIL" if mismatch is not None else "XPASS"
        print(f"{status} VTE-width/{name}: {mismatch or 'matched'}",
              file=sys.stderr)
        return 1
    print(f"{'XFAIL' if mismatch else 'PASS'} VTE-width/{name}: "
          f"{len(vectors)} codepoints" + (f"; {mismatch}" if mismatch else ""))
    stamp = Path(sys.argv[3])
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
