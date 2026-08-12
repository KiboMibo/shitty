#!/usr/bin/env python3

import os
import signal
import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from fuzz_parser import observable, state_difference
from harness import Shitty
from catalog import case_spec


def expected_trace(mode, payload, tokens):
    if not tokens:
        return []
    categories = {
        "control" if token[0] == "ctl"
        else "escape"
        if token[0] in ("esc", "esc_cs", "esc_de", "vt52")
        else "csi" if token[0].startswith("csi_")
        else "unknown"
        for token in tokens
    }
    if len(categories) != 1 or "unknown" in categories:
        raise ValueError(f"unsupported Konsole tokens: {tokens!r}")
    category, = categories
    if category == "control":
        if len(payload) != 1:
            raise ValueError(f"invalid control payload: {payload!r}")
        # ECMA-48 permits NUL to be ignored. Shitty drops it on its zero-run
        # fast path; Konsole emits an internal token and ignores it later.
        return [] if payload == b"\0" else [("control", payload)]
    if category == "escape":
        if not payload.startswith(b"\x1b"):
            raise ValueError(f"invalid escape payload: {payload!r}")
        return [("escape", payload[1:])]
    if mode != "ansi" or not payload.startswith(b"\x1b["):
        raise ValueError(f"invalid CSI payload: {payload!r}")
    return [("csi", payload[2:])]


def integers(fields):
    return tuple(int(field) if field else 0 for field in fields)


def sgr_tokens(body):
    result = []
    for group in body.split(b";"):
        fields = integers(group.split(b":"))
        if len(fields) > 1 and fields[0] == 38 and fields[1] == 2:
            if len(fields) not in (5, 6):
                raise ValueError(f"invalid colon RGB SGR: {body!r}")
            red, green, blue = fields[-3:]
            result.append(
                ("csi_ps", (ord("m"), 38), 4, red << 16 | green << 8 | blue)
            )
            continue
        if len(fields) > 1 and fields[0] == 38 and fields[1] == 5:
            if len(fields) != 3:
                raise ValueError(f"invalid colon indexed SGR: {body!r}")
            result.append(("csi_ps", (ord("m"), 38), 3, fields[2]))
            continue
        if len(fields) != 1:
            raise ValueError(f"unsupported colon SGR: {body!r}")
        result.append(("csi_ps", (ord("m"), fields[0]), 0, 0))
    if b":" in body:
        return tuple(result)

    values = integers(body.split(b";"))
    result = []
    index = 0
    while index < len(values):
        value = values[index]
        if value == 38 and index + 1 < len(values) and values[index + 1] == 2:
            if index + 4 >= len(values):
                raise ValueError(f"truncated RGB SGR: {body!r}")
            red, green, blue = values[index + 2:index + 5]
            result.append(
                ("csi_ps", (ord("m"), value), 4, red << 16 | green << 8 | blue)
            )
            index += 5
        elif (
            value == 38
            and index + 2 < len(values)
            and values[index + 1] == 5
        ):
            result.append(
                ("csi_ps", (ord("m"), value), 3, values[index + 2])
            )
            index += 3
        else:
            result.append(("csi_ps", (ord("m"), value), 0, 0))
            index += 1
    return tuple(result)


def csi_tokens(payload):
    final = payload[-1]
    body = payload[:-1]
    prefix = b""
    if body[:1] in (b"?", b"!", b"=", b">"):
        prefix, body = body[:1], body[1:]
    intermediate = b""
    if body.endswith(b" "):
        body, intermediate = body[:-1], b" "

    if prefix == b"?":
        return tuple(
            ("csi_pr", (final, parameter), index, 0)
            for index, parameter in enumerate(integers(body.split(b";")))
        )
    if prefix == b"!":
        return (("csi_pe", (final,), 0, 0),)
    if prefix == b"=":
        return (("csi_pq", (final,), 0, 0),)
    if prefix == b">":
        return (("csi_pg", (final,), 0, 0),)
    if intermediate == b" ":
        if body:
            return (("csi_psp", (final, int(body)), 0, 0),)
        return (("csi_sp", (final,), 0, 0),)
    if final == ord("m"):
        return sgr_tokens(body)

    values = integers(body.split(b";")) if body else (0,)
    if final in (ord("K"), ord("n"), ord("t")):
        return ((
            "csi_ps",
            (final, values[0]),
            values[1] if len(values) > 1 else 0,
            values[2] if len(values) > 2 else 0,
        ),)
    if final in (ord("@"), ord("H")):
        return ((
            "csi_pn",
            (final,),
            values[0],
            values[1] if len(values) > 1 else 0,
        ),)
    raise ValueError(f"unsupported Konsole CSI trace: {payload!r}")


def trace_tokens(mode, trace):
    if not trace:
        return ()
    if len(trace) != 1:
        raise ValueError(f"unexpected multiple parser events: {trace!r}")
    event, payload = trace[0]
    if event == "control":
        if len(payload) != 1:
            raise ValueError(f"invalid control trace: {payload!r}")
        return (("ctl", (payload[0] + ord("@"),), 0, 0),)
    if event == "escape":
        if mode == "vt52":
            return ((
                "vt52",
                (payload[0],),
                payload[1] if len(payload) > 1 else 0,
                payload[2] if len(payload) > 2 else 0,
            ),)
        if len(payload) == 1:
            return (("esc", (payload[0],), 0, 0),)
        if len(payload) == 2 and payload[0] == ord("#"):
            return (("esc_de", (payload[1],), 0, 0),)
        if len(payload) == 2:
            return (("esc_cs", (payload[0], payload[1]), 0, 0),)
        raise ValueError(f"unsupported Konsole escape trace: {payload!r}")
    if event == "csi":
        return csi_tokens(payload)
    raise ValueError(f"unsupported Konsole parser event: {event!r}")


def modern_tokens(tokens):
    # Konsole tokenizes standalone NUL before dropping it. ECMA-48 permits
    # receivers to ignore NUL, which Shitty does in the parser's zero-run path.
    return tuple(
        token
        for token in tokens
        if not (token[0] == "ctl" and token[1] == (ord("@"),))
    )


def exercise(mode, payload, chunked):
    with Shitty(columns=5, rows=5, save_lines=5) as terminal:
        if mode == "vt52":
            terminal.write(b"\x1b[?2l")
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
    timeout_message = f"FAIL Konsole/{name}: exceeded 10 seconds\n".encode()

    def timed_out(_signum, _frame):
        os.write(2, timeout_message)
        os._exit(124)

    signal.signal(signal.SIGALRM, timed_out)
    signal.alarm(10)
    label, mode, payload, tokens = case_spec(name)
    expected = expected_trace(mode, payload, tokens)
    expected_tokens = modern_tokens(tokens)
    whole = exercise(mode, payload, False)
    chunked = exercise(mode, payload, True)
    if whole["parser trace"] != expected:
        mismatch = (
            f"parser oracle: got={whole['parser trace']!r}, "
            f"expected={expected!r}"
        )
    elif chunked["parser trace"] != expected:
        mismatch = (
            f"chunked parser oracle: got={chunked['parser trace']!r}, "
            f"expected={expected!r}"
        )
    elif trace_tokens(mode, whole["parser trace"]) != expected_tokens:
        mismatch = (
            "token oracle: "
            f"got={trace_tokens(mode, whole['parser trace'])!r}, "
            f"expected={expected_tokens!r}"
        )
    elif trace_tokens(mode, chunked["parser trace"]) != expected_tokens:
        mismatch = (
            "chunked token oracle: "
            f"got={trace_tokens(mode, chunked['parser trace'])!r}, "
            f"expected={expected_tokens!r}"
        )
    else:
        mismatch = difference(whole, chunked)
    signal.alarm(0)
    if (mismatch is not None) != (name in known):
        status = "FAIL" if mismatch is not None else "XPASS"
        print(f"{status} Konsole/{name} ({label}): {mismatch or 'matched'}",
              file=sys.stderr)
        return 1
    if mismatch is not None:
        print(f"XFAIL Konsole/{name} ({label}): {mismatch}")
    else:
        print(
            f"PASS Konsole/{name} "
            f"({label}, {len(payload)} bytes, {len(expected)} parser events)"
        )
    stamp = Path(sys.argv[3])
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
