#!/usr/bin/env python3
"""Deterministic parser fuzz target using Zutty's headless protocol boundary."""

import argparse
import random

from harness import Zutty


CONTROL_BYTES = b"\x00\x07\x08\x09\x0a\x0d\x18\x1a\x1b\x7f\x84\x8d\x90\x98\x9b\x9c\x9d\x9e\x9f"
SYNTAX_BYTES = b"[]P\\^_;:?><=!\"' $*0123456789;:@ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz{|}~"


def fuzz(seed, cases, maximum):
    random_source = random.Random(seed)
    with Zutty(columns=37, rows=11, save_lines=64) as terminal:
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
            offset = 0
            while offset < len(payload):
                chunk = random_source.randrange(1, 18)
                terminal.write(payload[offset : offset + chunk])
                offset += chunk
            if case % 64 == 0:
                terminal.snapshot()
                terminal.read_input()
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
