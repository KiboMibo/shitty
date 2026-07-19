#!/usr/bin/env python3
"""Run one protocol probe under Zutty, xterm, foot and kitty."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from harness import Zutty
from terminal_probe import QUERIES


ROOT = Path(__file__).resolve().parent


def zutty_trace():
    result = {}
    with Zutty(columns=80, rows=24) as terminal:
        for name, query in QUERIES.items():
            terminal.write(query)
            result[name] = terminal.read_input().hex()
    return result


def terminal_command(name, output):
    probe = [sys.executable, str(ROOT / "terminal_probe.py"), str(output)]
    binary = shutil.which(name)
    if binary is None:
        return None
    if name == "foot":
        return [binary, "--log-level=error", "--window-size-chars=80x24", *probe]
    if name == "xterm":
        return [binary, "-geometry", "80x24", "-e", *probe]
    return [binary, "--config", "NONE", "--override", "remember_window_size=no",
            "--override", "initial_window_width=80c",
            "--override", "initial_window_height=24c", *probe]


def external_trace(name, directory):
    output = directory / f"{name}.json"
    command = terminal_command(name, output)
    if command is None:
        return None
    subprocess.run(command, check=True, timeout=15)
    return json.loads(output.read_text(encoding="utf-8"))


def validate(name, trace):
    decoded = {key: bytes.fromhex(value) for key, value in trace.items()}
    required = {
        "primary_da": rb"(?:\x1b\[|\x9b)\?[^c]*c",
        "secondary_da": rb"(?:\x1b\[|\x9b)>[^c]*c",
        "cursor_position": rb"(?:\x1b\[|\x9b)3;7R",
    }
    for key, pattern in required.items():
        if re.search(pattern, decoded[key]) is None:
            raise RuntimeError(f"{name}: malformed {key}: {decoded[key]!r}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--require", action="append", choices=("xterm", "foot", "kitty"))
    parser.add_argument("--output")
    arguments = parser.parse_args()
    traces = {"zutty": zutty_trace()}
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        for name in ("xterm", "foot", "kitty"):
            trace = external_trace(name, directory)
            if trace is not None:
                traces[name] = trace
            elif arguments.require and name in arguments.require:
                raise RuntimeError(f"required terminal is unavailable: {name}")
    for name, trace in traces.items():
        validate(name, trace)
    encoded = json.dumps(traces, sort_keys=True, indent=2)
    if arguments.output:
        Path(arguments.output).write_text(encoded + "\n", encoding="utf-8")
    else:
        print(encoded)


if __name__ == "__main__":
    main()
