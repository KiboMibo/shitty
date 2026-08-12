#!/usr/bin/env python3
# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

"""Run one protocol probe under Shitty, xterm, foot and kitty."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from harness import Shitty
from terminal_probe import QUERIES


ROOT = Path(__file__).resolve().parent


def shitty_trace():
    result = {}
    with Shitty(columns=80, rows=24) as terminal:
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
    try:
        subprocess.run(command, check=True, timeout=15)
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
        # An installed terminal that cannot start (no compositor/display)
        # counts as unavailable.
        print(f"differential: {name} failed to run: {error}", file=sys.stderr)
        return None
    if not output.exists():
        return None
    return json.loads(output.read_text(encoding="utf-8"))


def validate(name, trace):
    decoded = {key: bytes.fromhex(value) for key, value in trace.items()}
    required = {
        "primary_da": rb"(?:\x1b\[|\x9b)\?[^c]*c",
        "secondary_da": rb"(?:\x1b\[|\x9b)>[^c]*c",
        "cursor_position": rb"(?:\x1b\[|\x9b)3;7R",
        "cursor_mode": rb"(?:\x1b\[|\x9b)\?25;[12]\$y",
        "default_foreground": rb"(?:\x1b\]|\x9d)10;rgb:",
        "palette_red": rb"(?:\x1b\]|\x9d)4;1;rgb:",
    }
    for key, pattern in required.items():
        if re.search(pattern, decoded[key]) is None:
            raise RuntimeError(f"{name}: malformed {key}: {decoded[key]!r}")


def normalized(name, payload):
    # 8-bit C1 responses compare equal to their 7-bit forms.
    payload = payload.replace(b"\x9b", b"\x1b[")
    payload = payload.replace(b"\x9d", b"\x1b]")
    payload = payload.replace(b"\x9c", b"\x1b\\\\")
    if name in ("primary_da", "secondary_da"):
        # Device attributes legitimately differ per terminal; only the
        # response shape has to match.
        return re.sub(rb"[0-9;]+", b"#", payload)
    if name == "cursor_mode":
        # DECTCEM state may default differently; the report format may not.
        return re.sub(rb";[0-9]+\$y", b";#$y", payload)
    if name in ("default_foreground", "palette_red"):
        # Configured colors differ; the reply grammar and terminator style
        # have to agree.
        payload = re.sub(rb"rgb:[0-9a-fA-F/]+", b"rgb:#", payload)
        payload = re.sub(rb"(\x1b\\\\|\x07)$", b"<st>", payload)
        return payload
    # cursor_position and everything else must match byte for byte.
    return payload


def compare(traces):
    failures = []
    for reference in traces:
        if reference == "shitty":
            continue
        for key in traces["shitty"]:
            ours = normalized(key, bytes.fromhex(traces["shitty"][key]))
            theirs = normalized(key, bytes.fromhex(traces[reference][key]))
            if ours != theirs:
                failures.append(
                    f"{key}: shitty {ours!r} != {reference} {theirs!r}"
                )
    return failures


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--require", action="append", choices=("xterm", "foot", "kitty"))
    parser.add_argument("--output")
    arguments = parser.parse_args()
    traces = {"shitty": shitty_trace()}
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
    failures = compare(traces)
    encoded = json.dumps(traces, sort_keys=True, indent=2)
    if arguments.output:
        Path(arguments.output).write_text(encoded + "\n", encoding="utf-8")
    else:
        print(encoded)
    if len(traces) == 1:
        print(
            "differential: no reference terminal available; "
            "self-validation only, nothing was compared",
            file=sys.stderr,
        )
    for failure in failures:
        print(f"differential: {failure}", file=sys.stderr)
    if failures:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
