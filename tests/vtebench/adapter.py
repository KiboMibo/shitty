#!/usr/bin/env python3

import signal
import sys
import time
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Zutty


COLUMNS = 80
ROWS = 24


def setup_payload(case):
    if case in ("dense_cells", "light_cells", "unicode"):
        return b"\x1b[?1049h"
    if case in ("scrolling", "scrolling_fullscreen"):
        return b"y\n" * 100001
    if case == "scrolling_bottom_region":
        return f"\x1b[?1049h\x1b[1;{ROWS - 1}r".encode()
    if case == "scrolling_bottom_small_region":
        return f"\x1b[?1049h\x1b[1;{ROWS // 2}r".encode()
    if case == "scrolling_top_region":
        return f"\x1b[?1049h\x1b[2;{ROWS}r".encode()
    if case == "scrolling_top_small_region":
        return f"\x1b[?1049h\x1b[{ROWS // 2};{ROWS}r".encode()
    return b""


def cursor_motion():
    output = bytearray()
    for character in b"ABCDEFGHIJKLMNOPQRSTUVWXYZ":
        column_start, column_end = 1, COLUMNS
        line_start, line_end = 1, ROWS
        while True:
            column, line = column_start, line_start
            while column < column_end:
                output.extend(f"\x1b[{line};{column}H".encode())
                output.append(character)
                column += 1
            while line < line_end:
                output.extend(f"\x1b[{line};{column}H".encode())
                output.append(character)
                line += 1
            while column > column_start:
                output.extend(f"\x1b[{line};{column}H".encode())
                output.append(character)
                column -= 1
            while line > line_start:
                output.extend(f"\x1b[{line};{column}H".encode())
                output.append(character)
                line -= 1
            column_start += 1
            line_start += 1
            column_end -= 1
            line_end -= 1
            if column_start > column_end or line_start > line_end:
                break
    return bytes(output)


def dense_cells():
    output = bytearray()
    offset = 0
    for character in b"ABCDEFGHIJKLMNOPQRSTUVWXYZ":
        output.extend(b"\x1b[H")
        for line in range(1, ROWS + 1):
            for column in range(1, COLUMNS + 1):
                index = line + column + offset
                foreground = index % 156 + 100
                background = 255 - index % 156 + 100
                output.extend(
                    f"\x1b[38;5;{foreground};48;5;{background};1;3;4m".encode()
                )
                output.append(character)
        offset += 1
    return bytes(output)


def benchmark_payload(root, case):
    if case == "cursor_motion":
        return cursor_motion()
    if case == "dense_cells":
        return dense_cells()
    if case == "light_cells":
        return b"".join(
            b"\x1b[H" + bytes((character,)) * (COLUMNS * ROWS)
            for character in b"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        )
    if case in ("medium_cells", "sync_medium_cells"):
        return (root / "benchmarks" / case / "vim_session").read_bytes()
    if case == "scrolling_fullscreen":
        return b"".join(
            bytes((character,)) * COLUMNS + b"\n"
            for character in b"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        )
    if case == "unicode":
        return (root / "benchmarks" / case / "symbols").read_bytes()
    return b"y\n"


def write_chunked(terminal, payload):
    for offset in range(0, len(payload), 65521):
        terminal.write(payload[offset : offset + 65521])


def observable(terminal):
    return (
        terminal.read_input(),
        terminal.read_actions(),
        terminal.read_printer(),
        terminal.state(),
        terminal.protocol_state(),
        terminal.conformance_state(),
        terminal.render_state(),
        terminal.hyperlink_count(),
        terminal.model_digest(),
    )


def main():
    if len(sys.argv) != 5:
        raise SystemExit("usage: adapter.py CASE XFAIL_FILE STAMP TIMEOUT")
    case = sys.argv[1]
    known_failures = {
        line.strip() for line in Path(sys.argv[2]).read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }
    stamp = Path(sys.argv[3])
    timeout = int(sys.argv[4])
    root = Path(__file__).resolve().parent
    payload = setup_payload(case) + benchmark_payload(root, case)
    signal.alarm(timeout)

    with Zutty(columns=COLUMNS, rows=ROWS, save_lines=500) as whole, \
         Zutty(columns=COLUMNS, rows=ROWS, save_lines=500) as chunked:
        started = time.monotonic()
        whole.write(payload)
        elapsed = time.monotonic() - started
        write_chunked(chunked, payload)
        whole_state = observable(whole)
        chunked_state = observable(chunked)

    mismatch = None
    if whole_state != chunked_state:
        mismatch = "whole and chunked observable terminal state differ"
    if case in known_failures:
        if mismatch is None:
            print(f"XPASS vtebench/{case}", file=sys.stderr)
            return 1
        print(f"XFAIL vtebench/{case}: {mismatch}")
    elif mismatch is not None:
        print(f"FAIL vtebench/{case}: {mismatch}", file=sys.stderr)
        return 1

    rate = len(payload) / max(elapsed, 1e-9) / (1024 * 1024)
    print(
        f"PASS vtebench/{case}: {len(payload)} bytes, "
        f"{elapsed:.6f}s, {rate:.2f} MiB/s"
    )
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
