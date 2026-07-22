#!/usr/bin/env python3

import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from fuzz_parser import observable, state_difference
from harness import Shitty


CHUNK_SIZES = (1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 233, 377,
               610, 987, 1597, 2584, 4096)


def terminal_payload(member, raw):
    corpus = member.split("/", 1)[0]
    if corpus == "parser-cmin":
        return raw
    if corpus == "stream-cmin":
        return raw[1:]
    if corpus == "osc-cmin":
        selector = raw[0]
        terminator = (b"\x07", b"\x9c", b"")[selector % 3]
        return b"\x1b]" + raw[1:] + terminator
    raise ValueError(f"unknown Ghostty corpus: {corpus}")


def write_chunked(terminal, payload):
    offset = 0
    chunk_index = 0
    while offset < len(payload):
        size = CHUNK_SIZES[chunk_index % len(CHUNK_SIZES)]
        terminal.write(payload[offset : offset + size])
        offset += size
        chunk_index += 1


def reset(terminal):
    terminal.write(b"\x18\x1bc")
    terminal.read_input()
    terminal.read_actions()
    terminal.read_printer()


def visible_hyperlinks(terminal, snapshot):
    links = {}
    for index, cell in enumerate(snapshot.cells):
        if not cell.hyperlink or cell.hyperlink in links:
            continue
        row, column = divmod(index, snapshot.columns)
        links[cell.hyperlink] = terminal.hyperlink_bytes(column, row)
    return links


def compare_member(whole, chunked, member, raw):
    reset(whole)
    reset(chunked)
    payload = terminal_payload(member, raw)
    whole.write(payload)
    write_chunked(chunked, payload)

    comparisons = (
        ("PTY reply", whole.read_input(), chunked.read_input()),
        ("actions", whole.read_actions(), chunked.read_actions()),
        ("printer output", whole.read_printer(), chunked.read_printer()),
        ("input state", whole.state(), chunked.state()),
        ("protocol state", whole.protocol_state(), chunked.protocol_state()),
        (
            "conformance state",
            whole.conformance_state(),
            chunked.conformance_state(),
        ),
        ("render state", whole.render_state(), chunked.render_state()),
        (
            "hyperlink count",
            whole.hyperlink_count(),
            chunked.hyperlink_count(),
        ),
    )
    for name, left, right in comparisons:
        if left != right:
            return f"{name} differs: whole={left!r} chunked={right!r}"

    whole_digest = whole.model_digest()
    chunked_digest = chunked.model_digest()
    if whole_digest != chunked_digest:
        whole_state = observable(whole)
        chunked_state = observable(chunked)
        if whole_state != chunked_state:
            return state_difference(whole_state, chunked_state)
        return (
            "model digest differs but rich snapshot does not: "
            f"whole={whole_digest!r} chunked={chunked_digest!r}"
        )

    if comparisons[-1][1]:
        whole_snapshot = whole.model_snapshot()
        chunked_snapshot = chunked.model_snapshot()
        whole_links = visible_hyperlinks(whole, whole_snapshot)
        chunked_links = visible_hyperlinks(chunked, chunked_snapshot)
        if whole_links != chunked_links:
            return (
                "visible hyperlinks differ: "
                f"whole={whole_links!r} chunked={chunked_links!r}"
            )
    return None


def main():
    if len(sys.argv) < 4:
        raise SystemExit(
            "usage: adapter.py XFAIL_FILE STAMP CORPUS/MEMBER [...]"
        )
    xfail_path = Path(sys.argv[1])
    stamp = Path(sys.argv[2])
    members = sys.argv[3:]
    root = Path(__file__).resolve().parent
    known_failures = {
        line.strip() for line in xfail_path.read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }

    with (
        Shitty(columns=80, rows=24, save_lines=100) as whole,
        Shitty(columns=80, rows=24, save_lines=100) as chunked,
    ):
        for member in members:
            try:
                mismatch = compare_member(whole, chunked, member,
                                          (root / member).read_bytes())
            except Exception as error:
                raise RuntimeError(f"Ghostty/{member}: {error}") from error
            if member in known_failures:
                if mismatch is None:
                    print(f"XPASS Ghostty/{member}", file=sys.stderr)
                    return 1
                print(f"XFAIL Ghostty/{member}: {mismatch}")
            elif mismatch is not None:
                print(f"FAIL Ghostty/{member}: {mismatch}", file=sys.stderr)
                print(
                    "single member: python3 tests/ghostty/adapter.py "
                    "tests/ghostty/xfail.txt /tmp/ghostty.stamp " + member,
                    file=sys.stderr,
                )
                return 1

    print(f"PASS Ghostty/{members[0]}.. ({len(members)} members)")
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
