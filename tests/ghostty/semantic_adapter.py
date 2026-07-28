#!/usr/bin/env python3

import os
import signal
import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from fuzz_parser import observable, state_difference
from harness import Shitty
from semantic_catalog import case_payload


def exercise(chunks, mode):
    payload = b"".join(chunks)
    with Shitty(columns=80, rows=24, save_lines=100) as terminal:
        terminal.parser_trace_on()
        if mode == "whole":
            terminal.write(payload)
        elif mode == "upstream":
            terminal.write_chunks(*chunks)
        elif mode == "bytewise":
            terminal.write_chunks(*(payload[index:index + 1]
                                    for index in range(len(payload))))
        else:
            raise ValueError(mode)
        return {
            "parser trace": terminal.parser_trace(),
            "PTY reply": terminal.read_input(),
            "actions": terminal.read_actions(),
            "input state": terminal.state(),
            "protocol state": terminal.protocol_state(),
            "conformance state": terminal.conformance_state(),
            "render state": terminal.render_state(),
            "model": observable(terminal),
            "model digest": terminal.model_digest(),
        }


def difference(reference, candidate, mode):
    for field in reference:
        if reference[field] == candidate[field]:
            continue
        if field == "model":
            detail = state_difference(reference[field], candidate[field])
        else:
            detail = f"whole={reference[field]!r}, {mode}={candidate[field]!r}"
        return f"{mode} {field}: {detail}"
    return None


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: semantic_adapter.py CASE XFAIL_FILE STAMP")
    name = sys.argv[1]
    known = {
        line.strip() for line in Path(sys.argv[2]).read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }
    timeout_message = f"FAIL Ghostty/{name}: exceeded 10 seconds\n".encode()

    def timed_out(_signum, _frame):
        os.write(2, timeout_message)
        os._exit(124)

    signal.signal(signal.SIGALRM, timed_out)
    signal.alarm(10)
    label, chunks = case_payload(name)
    whole = exercise(chunks, "whole")
    mismatch = difference(whole, exercise(chunks, "upstream"), "upstream")
    if mismatch is None:
        mismatch = difference(whole, exercise(chunks, "bytewise"), "bytewise")
    signal.alarm(0)
    if (mismatch is not None) != (name in known):
        status = "FAIL" if mismatch is not None else "XPASS"
        print(f"{status} Ghostty/{name} ({label}): {mismatch or 'matched'}",
              file=sys.stderr)
        return 1
    if mismatch is not None:
        print(f"XFAIL Ghostty/{name} ({label}): {mismatch}")
    else:
        print(f"PASS Ghostty/{name} ({label}, {len(chunks)} chunks)")
    stamp = Path(sys.argv[3])
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
