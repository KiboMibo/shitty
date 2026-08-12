#!/usr/bin/env python3

import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from fuzz_parser import observable, state_difference
from harness import Shitty


def reset(terminal):
    terminal.write(b"\x18\x1bc")
    terminal.read_input()
    terminal.read_actions()


def compare_member(whole, bytewise, payload):
    reset(whole)
    reset(bytewise)
    whole.write(payload)
    for byte in payload:
        bytewise.write(bytes((byte,)))

    whole_reply = whole.read_input()
    bytewise_reply = bytewise.read_input()
    if whole_reply != bytewise_reply:
        return (
            f"PTY reply differs: whole={whole_reply.hex()} "
            f"bytewise={bytewise_reply.hex()}"
        )
    whole_actions = whole.read_actions()
    bytewise_actions = bytewise.read_actions()
    if whole_actions != bytewise_actions:
        return (
            f"actions differ: whole={whole_actions!r} "
            f"bytewise={bytewise_actions!r}"
        )
    whole_modes = whole.conformance_state()
    bytewise_modes = bytewise.conformance_state()
    if whole_modes != bytewise_modes:
        return (
            f"modes differ: whole={whole_modes!r} "
            f"bytewise={bytewise_modes!r}"
        )
    whole_state = observable(whole)
    bytewise_state = observable(bytewise)
    if whole_state != bytewise_state:
        return state_difference(whole_state, bytewise_state)
    return None


def main():
    if len(sys.argv) not in (4, 5):
        raise SystemExit("usage: adapter.py CORPUS XFAIL_FILE STAMP [MEMBER]")
    corpus = sys.argv[1]
    xfail_path = Path(sys.argv[2])
    stamp = Path(sys.argv[3])
    selected = sys.argv[4] if len(sys.argv) == 5 else None
    root = Path(__file__).resolve().parent
    known_failures = {
        line.strip() for line in xfail_path.read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }
    seen = set()

    with (
        Shitty(columns=80, rows=24, save_lines=100) as whole,
        Shitty(columns=80, rows=24, save_lines=100) as bytewise,
    ):
        paths = sorted((root / corpus).iterdir())
        if selected is not None:
            paths = [path for path in paths if path.name == selected]
            if not paths:
                raise SystemExit(f"unknown corpus member: {corpus}/{selected}")
        for path in paths:
            member = f"{corpus}/{path.name}"
            mismatch = compare_member(whole, bytewise, path.read_bytes())
            if member in known_failures:
                seen.add(member)
                if mismatch is None:
                    print(f"XPASS Mosh/{member}", file=sys.stderr)
                    return 1
                print(f"XFAIL Mosh/{member}: {mismatch}")
            elif mismatch is not None:
                print(f"FAIL Mosh/{member}: {mismatch}", file=sys.stderr)
                print(
                    "single member: python3 tst/mosh/adapter.py "
                    f"{corpus} tst/mosh/xfail.txt /tmp/mosh.stamp {path.name}",
                    file=sys.stderr,
                )
                return 1

    stale = sorted(
        failure for failure in known_failures
        if failure.startswith(corpus + "/") and failure not in seen
    ) if selected is None else []
    if stale:
        print("unknown XFAIL members: " + ", ".join(stale), file=sys.stderr)
        return 1
    print(f"PASS Mosh/{corpus}")
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
