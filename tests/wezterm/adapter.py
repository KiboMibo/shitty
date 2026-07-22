#!/usr/bin/env python3

import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from fuzz_parser import observable, state_difference
from harness import Shitty
from catalog import case_payload


def exercise(payload, chunked):
    with Shitty(columns=5, rows=5, save_lines=5) as terminal:
        terminal.parser_trace_on()
        if chunked:
            terminal.write_chunks(*(payload[index:index + 1]
                                    for index in range(len(payload))))
        else:
            terminal.write(payload)
        return {
            "parser trace": terminal.parser_trace(),
            "PTY reply": terminal.read_input(),
            "actions": terminal.read_actions(),
            "printer output": terminal.read_printer(),
            "input state": terminal.state(),
            "protocol state": terminal.protocol_state(),
            "conformance state": terminal.conformance_state(),
            "render state": terminal.render_state(),
            "model": observable(terminal),
            "model digest": terminal.model_digest(),
        }


def difference(whole, chunked):
    for field in whole:
        if whole[field] == chunked[field]:
            continue
        if field == "model":
            return "model " + state_difference(whole[field], chunked[field])
        return f"{field}: whole={whole[field]!r}, chunked={chunked[field]!r}"
    return None


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: adapter.py CASE XFAIL_FILE STAMP")
    name = sys.argv[1]
    known = {
        line.strip() for line in Path(sys.argv[2]).read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }
    payload = case_payload(name)
    mismatch = difference(exercise(payload, False), exercise(payload, True))
    if (mismatch is not None) != (name in known):
        status = "FAIL" if mismatch is not None else "XPASS"
        print(f"{status} WezTerm/{name}: {mismatch or 'matched'}", file=sys.stderr)
        return 1
    if mismatch is not None:
        print(f"XFAIL WezTerm/{name}: {mismatch}")
    else:
        print(f"PASS WezTerm/{name} ({len(payload)} bytes)")
    stamp = Path(sys.argv[3])
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
