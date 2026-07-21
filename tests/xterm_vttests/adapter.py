#!/usr/bin/env python3

import os
import signal
import subprocess
import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Zutty


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


def generate(root, case):
    environment = os.environ.copy()
    environment["PATH"] = str(root / "bin") + os.pathsep + environment["PATH"]
    temporary = root.parent.parent / ".build" / "xterm-vttests-tmp"
    temporary.mkdir(parents=True, exist_ok=True)
    environment["TMPDIR"] = str(temporary)
    result = subprocess.run(
        [str(root / "upstream" / case)],
        input=b"\n\n",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=environment,
        timeout=10,
        check=False,
    )
    accepted_codes = {0, 1} if case == "bounce.sh" else {0}
    if result.returncode not in accepted_codes:
        raise RuntimeError(
            f"{case} exited {result.returncode}: "
            + result.stderr.decode(errors="replace")
        )
    if not result.stdout:
        raise RuntimeError(f"{case} produced an empty stream")
    return result.stdout


def write_chunked(terminal, payload):
    sizes = (1, 2, 3, 5, 8, 13, 21, 34)
    offset = 0
    index = 0
    while offset < len(payload):
        size = sizes[index % len(sizes)]
        terminal.write(payload[offset : offset + size])
        offset += size
        index += 1


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: adapter.py SCRIPT XFAIL_FILE STAMP")
    case = sys.argv[1]
    xfails = {
        line.strip() for line in Path(sys.argv[2]).read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }
    stamp = Path(sys.argv[3])
    root = Path(__file__).resolve().parent
    signal.alarm(20)
    payload = generate(root, case)
    with Zutty(columns=80, rows=25, save_lines=500) as whole, \
         Zutty(columns=80, rows=25, save_lines=500) as chunked:
        whole.write(payload)
        write_chunked(chunked, payload)
        mismatch = observable(whole) != observable(chunked)
    if case in xfails:
        if not mismatch:
            print(f"XPASS xterm-vttests/{case}", file=sys.stderr)
            return 1
        print(f"XFAIL xterm-vttests/{case}")
    elif mismatch:
        print(f"FAIL xterm-vttests/{case}: chunking changed state", file=sys.stderr)
        return 1
    else:
        print(f"PASS xterm-vttests/{case}: {len(payload)} stream bytes")
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
