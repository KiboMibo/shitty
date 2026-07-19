#!/usr/bin/env python3
"""Run upstream vttest through Zutty's real PTY and headless screen."""

import argparse
import os
import shutil
import tempfile
import time

from harness import Zutty


def run(binary, rounds):
    with tempfile.NamedTemporaryFile("w", encoding="ascii") as commands:
        commands.write("Read: *\n")
        commands.write("Read: 0\n" * 4096)
        commands.flush()
        with Zutty(columns=80, rows=24, save_lines=2000) as terminal:
            terminal.spawn(binary, "-q", "-u", "-c", commands.name)
            saw_banner = False
            for _ in range(rounds):
                terminal.pump()
                snapshot = terminal.snapshot()
                saw_banner |= any("VT100 test program" in line for line in snapshot.lines)
                terminal.char(" ")
                time.sleep(0.005)
            if not saw_banner:
                raise RuntimeError("vttest did not reach its main test program")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", default=os.environ.get("VTTEST_BINARY", "vttest"))
    parser.add_argument("--rounds", type=int, default=1200)
    arguments = parser.parse_args()
    binary = shutil.which(arguments.binary)
    if binary is None:
        print("SKIP: upstream vttest is not installed")
        return
    run(binary, arguments.rounds)


if __name__ == "__main__":
    main()
