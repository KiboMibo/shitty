#!/usr/bin/env python3
"""Deterministic parser fuzz target using Shitty's headless protocol boundary."""

import argparse
import random

from harness import Shitty


CONTROL_BYTES = b"\x00\x07\x08\x09\x0a\x0d\x18\x1a\x1b\x7f\x84\x8d\x90\x98\x9b\x9c\x9d\x9e\x9f"
SYNTAX_BYTES = b"[]P\\^_;:?><=!\"' $*0123456789;:@ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz{|}~"


def observable(terminal):
    snapshot = terminal.snapshot()
    cursor_x = snapshot.cursor_x if snapshot.cursor_style else -1
    cursor_y = snapshot.cursor_y if snapshot.cursor_style else -1
    return (
        snapshot.columns,
        snapshot.rows,
        cursor_x,
        cursor_y,
        snapshot.cursor_style,
        snapshot.view_offset,
        snapshot.selection,
        snapshot.rectangular_selection,
        tuple(snapshot.cells),
    )


def state_difference(lhs, rhs):
    names = (
        "columns", "rows", "cursor_x", "cursor_y", "cursor_style",
        "view_offset", "selection", "rectangular_selection", "cells",
    )
    for index, name in enumerate(names):
        if lhs[index] == rhs[index]:
            continue
        if name == "cells":
            for cell_index, (left, right) in enumerate(
                zip(lhs[index], rhs[index])
            ):
                if left != right:
                    return f"cell {cell_index}: {left!r} != {right!r}"
        return f"{name}: {lhs[index]!r} != {rhs[index]!r}"
    return "unknown difference"


def fuzz(seed, cases, maximum):
    random_source = random.Random(seed)
    with (
        Shitty(columns=37, rows=11, save_lines=64) as whole,
        Shitty(columns=37, rows=11, save_lines=64) as chunked,
    ):
        for case in range(cases):
            length = random_source.randrange(maximum + 1)
            payload = bytearray()
            for _ in range(length):
                choice = random_source.randrange(10)
                if choice < 3:
                    payload.append(random_source.choice(CONTROL_BYTES))
                elif choice < 7:
                    payload.append(random_source.choice(SYNTAX_BYTES))
                else:
                    payload.append(random_source.randrange(256))
            whole.write(payload)
            offset = 0
            while offset < len(payload):
                chunk = random_source.randrange(1, 18)
                chunked.write(payload[offset : offset + chunk])
                offset += chunk
            if whole.read_input() != chunked.read_input():
                raise RuntimeError(f"PTY reply differs after case {case}")
            if whole.read_actions() != chunked.read_actions():
                raise RuntimeError(f"action stream differs after case {case}")
            if case % 32 == 0:
                whole_state = observable(whole)
                chunked_state = observable(chunked)
                if whole_state != chunked_state:
                    raise RuntimeError(
                        f"terminal state differs after case {case}: "
                        f"{state_difference(whole_state, chunked_state)}; "
                        f"payload={payload.hex()}"
                    )
        whole_state = observable(whole)
        chunked_state = observable(chunked)
        if whole_state != chunked_state:
            raise RuntimeError(
                "final terminal states differ: "
                + state_difference(whole_state, chunked_state)
            )
        for terminal in (whole, chunked):
            terminal.write(b"\x18\x1bcOK")
            if not terminal.snapshot().lines[0].startswith("OK"):
                raise RuntimeError("parser did not recover after fuzz input")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--seed", type=int, default=0x5A7759)
    parser.add_argument("--cases", type=int, default=1500)
    parser.add_argument("--maximum", type=int, default=192)
    arguments = parser.parse_args()
    fuzz(arguments.seed, arguments.cases, arguments.maximum)


if __name__ == "__main__":
    main()
