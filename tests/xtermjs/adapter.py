#!/usr/bin/env python3

import difflib
import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Zutty


def normalized(text):
    return "\n".join(line.rstrip() for line in text.split("\n"))


def through_default_pty(data):
    # xterm.js writes every fixture to a freshly opened slave PTY. Linux's
    # default OPOST|ONLCR output processing expands every LF, including the LF
    # in an existing CRLF pair.
    return data.replace(b"\n", b"\r\n")


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: adapter.py CASE XFAIL_FILE STAMP")

    case = sys.argv[1]
    xfail_path = Path(sys.argv[2])
    stamp = Path(sys.argv[3])
    data = Path(__file__).resolve().parent
    known_failures = {
        line.strip()
        for line in xfail_path.read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }

    with Zutty(columns=80, rows=25) as zutty:
        zutty.write(b"\x1bc\x1b[H")
        zutty.write(through_default_pty((data / f"{case}.in").read_bytes()))
        actual = "\n".join(line.rstrip() for line in zutty.snapshot().lines) + "\n"

    expected = normalized((data / f"{case}.text").read_text())
    actual = normalized(actual)
    matches = actual == expected

    if case in known_failures:
        if matches:
            print(f"XPASS xterm.js/{case}", file=sys.stderr)
            return 1
        print(f"XFAIL xterm.js/{case}")
    elif not matches:
        print(f"FAIL xterm.js/{case}", file=sys.stderr)
        difference = list(difflib.unified_diff(
            expected.splitlines(keepends=True),
            actual.splitlines(keepends=True),
            fromfile=f"{case}.text",
            tofile="zutty snapshot",
        ))
        print("".join(difference[:200]), file=sys.stderr)
        if len(difference) > 200:
            print(f"... {len(difference) - 200} diff lines omitted", file=sys.stderr)
        return 1
    else:
        print(f"PASS xterm.js/{case}")

    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
