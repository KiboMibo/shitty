#!/usr/bin/env python3
# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

"""Capture protocol replies from the terminal hosting this process."""

import json
import os
import select
import sys
import termios
import time
import tty


QUERIES = {
    "primary_da": b"\x1b[c",
    "secondary_da": b"\x1b[>c",
    "cursor_position": b"\x1b[3;7H\x1b[6n",
    "cursor_mode": b"\x1b[?25$p",
    "default_foreground": b"\x1b]10;?\x07",
    "palette_red": b"\x1b]4;1;?\x07",
}


def read_reply(fd):
    result = bytearray()
    deadline = time.monotonic() + 1.0
    while time.monotonic() < deadline:
        ready, _, _ = select.select([fd], [], [], 0.08 if result else 0.2)
        if not ready:
            if result:
                break
            continue
        chunk = os.read(fd, 4096)
        if not chunk:
            break
        result.extend(chunk)
    return bytes(result)


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: terminal_probe.py OUTPUT.json")
    fd = sys.stdin.fileno()
    original = termios.tcgetattr(fd)
    replies = {}
    try:
        tty.setraw(fd)
        for name, query in QUERIES.items():
            os.write(sys.stdout.fileno(), query)
            replies[name] = read_reply(fd).hex()
    finally:
        termios.tcsetattr(fd, termios.TCSANOW, original)
    with open(sys.argv[1], "w", encoding="utf-8") as output:
        json.dump(replies, output, sort_keys=True, indent=2)


if __name__ == "__main__":
    main()
