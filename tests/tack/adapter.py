#!/usr/bin/env python3

import os
import shutil
import sys
import time
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Shitty


STARTED = time.monotonic()


def phase(capability, name):
    # Eager and unbuffered: when the 60-second harness reaper kills a hung
    # case, these lines are the only record of how far it got.
    elapsed = time.monotonic() - STARTED
    print(f"tack/{capability} {name} +{elapsed:.2f}s", file=sys.stderr, flush=True)


def wait_for(terminal, text, timeout=4.0):
    deadline = time.monotonic() + timeout
    screen = ""
    while time.monotonic() < deadline:
        status, screen = terminal.poll_child()
        if status is not None:
            return status, screen
        if text in screen:
            return None, screen
        time.sleep(0.01)
    raise TimeoutError(f"tack did not display {text!r}")


def run_case(binary, capability):
    environment = shutil.which("env")
    if environment is None:
        return "cannot find env"

    try:
        with Shitty(columns=132, rows=49, save_lines=500) as terminal:
            phase(capability, "ready")
            terminal.spawn(
                environment,
                "TERM=xterm-256color",
                "LC_ALL=C.UTF-8",
                str(binary),
                "-i",
            )
            status, _ = wait_for(terminal, "tack [n] >")
            if status is not None:
                return f"tack exited {status} before its main menu"

            phase(capability, "main menu")
            terminal.input(b"n")
            status, _ = wait_for(terminal, "Main test menu")
            if status is not None:
                return f"tack exited {status} before its test menu"

            phase(capability, "test menu")
            terminal.input(b"/")
            status, _ = wait_for(terminal, "enter name:")
            if status is not None:
                return f"tack exited {status} before its capability prompt"

            phase(capability, "capability prompt")
            before = terminal.model_digest()
            terminal.input(capability.encode("ascii") + b"\n")
            deadline = time.monotonic() + 0.75
            status = None
            screen = ""
            while time.monotonic() < deadline:
                status, screen = terminal.poll_child()
                if status is not None:
                    break
                terminal.input(b"\n")
                time.sleep(0.025)
            phase(capability, "scenario observed")
            after = terminal.model_digest()
            phase(capability, "closing")
    except (OSError, RuntimeError, TimeoutError) as error:
        return str(error)
    phase(capability, "closed")

    if status not in (None, 0):
        return f"tack exited {status}"
    if "Cap not found:" in screen:
        return "tack rejected the imported capability"
    if after == before:
        return "capability scenario produced no observable terminal state"
    return ""


def main():
    if len(sys.argv) != 5:
        raise SystemExit("usage: adapter.py TACK CAPABILITY XFAIL STAMP")
    binary = Path(sys.argv[1])
    capability = sys.argv[2]
    xfail = {
        line.strip()
        for line in Path(sys.argv[3]).read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }
    failure = run_case(binary, capability)
    expected = capability in xfail
    if failure:
        if expected:
            print(f"XFAIL tack/{capability}: {failure}")
            return 0
        print(f"FAIL tack/{capability}: {failure}", file=sys.stderr)
        return 1
    if expected:
        print(f"XPASS tack/{capability}", file=sys.stderr)
        return 1

    stamp = Path(sys.argv[4])
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    print(f"PASS tack/{capability}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
