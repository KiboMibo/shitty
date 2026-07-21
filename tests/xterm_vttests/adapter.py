#!/usr/bin/env python3

import os
import signal
import shutil
import subprocess
import sys
import time
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Zutty


LIVE_CASES = {
    "acolors.sh",
    "dynamic.sh",
    "dynamic2.sh",
    "fonts.sh",
    "resize.sh",
    "tab0.sh",
    "title.sh",
    "version.sh",
}

PREFIX_CASES = {
    "16colors.sh",
    "8colors.sh",
}

PERL_GENERATOR_CASES = {
    "256colors2.pl",
    "88colors2.pl",
    "acs.pl",
    "blink.pl",
    "bold-italics.pl",
    "erase.pl",
    "iso2022.pl",
    "modify-keys.pl",
    "nrcs.pl",
    "print-vt-chars.pl",
    "sgrPushPop.pl",
    "sgrPushPop2.pl",
    "vt52chars.pl",
    "wrap.pl",
}

PERL_LIVE_CASES = {
    "256colors.pl",
    "88colors.pl",
    "closest-rgb.pl",
    "cursor.pl",
    "decsed.pl",
    "dynamic.pl",
    "halves.pl",
    "insdelln.pl",
    "query-allowed.pl",
    "query-color.pl",
    "query-dynamic.pl",
    "query-fonts.pl",
    "query-status.pl",
    "query-xres.pl",
    "resize.pl",
    "scroll.pl",
    "setpos.pl",
    "tcapquery.pl",
}


def command_for(root, case):
    script = str(root / "upstream" / case)
    if case.endswith(".pl"):
        perl = shutil.which("perl")
        if perl is None:
            raise RuntimeError("xterm Perl scenarios require bld/perl")
        arguments = [perl, script]
        if case == "nrcs.pl":
            arguments.extend((
                "graphic", "supp", "supp_graphic", "technical", "latin_1",
                "ascii", "dutch", "finnish", "finnish2", "french",
                "french2", "canadian", "canadian2", "german", "italian",
                "danish", "danish2", "danish3", "portuguese", "spanish",
                "swedish", "swedish2", "swiss",
            ))
        return arguments
    return [script]


def command_path(root, case):
    path = str(root / "bin") + os.pathsep + os.environ["PATH"]
    if case.endswith(".pl"):
        path = str(Path(command_for(root, case)[0]).parent) + os.pathsep + path
    return path


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
    environment["PATH"] = command_path(root, case)
    temporary = root.parent.parent / ".build" / "xterm-vttests-tmp"
    temporary.mkdir(parents=True, exist_ok=True)
    environment["TMPDIR"] = str(temporary)
    result = subprocess.run(
        command_for(root, case),
        input=b"\n" * 128,
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


def generate_prefix(root, case, limit=256 * 1024):
    environment = os.environ.copy()
    environment["PATH"] = str(root / "bin") + os.pathsep + environment["PATH"]
    process = subprocess.Popen(
        [str(root / "upstream" / case)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=environment,
    )
    try:
        payload = process.stdout.read(limit)
    finally:
        process.kill()
        process.communicate(timeout=5)
    if len(payload) != limit:
        raise RuntimeError(
            f"{case} ended before producing a {limit}-byte stream prefix"
        )
    return payload


def write_chunked(terminal, payload):
    sizes = (1, 7, 31, 127, 509, 2039, 8191, 32749)
    offset = 0
    index = 0
    while offset < len(payload):
        size = sizes[index % len(sizes)]
        terminal.write(payload[offset : offset + size])
        offset += size
        index += 1


def run_pty_case(root, case):
    arguments = [str(root / "upstream" / case)]
    columns = 80
    rows = 25
    if case == "doublechars.sh":
        arguments.append("-n")
        columns = 132
        rows = 30
    with Zutty(columns=columns, rows=rows, save_lines=500) as terminal:
        terminal.spawn(*arguments)
        status, screen = terminal.wait_child(timeout=10)
        digest = terminal.model_digest()
    if status != 0:
        return f"child exited {status}"
    if case == "doublechars.sh" and "The quick brown fox" not in screen:
        return "double-size text was not visible in the final screen"
    if digest == (0, 0):
        return "empty model digest"
    return ""


def run_live_case(root, case):
    with Zutty(
        columns=80,
        rows=25,
        save_lines=500,
        extra_arguments=("-allowWindowOps", "true"),
    ) as terminal:
        before = observable(terminal)
        arguments = command_for(root, case)
        if case == "tab0.sh" or case.endswith(".pl"):
            arguments = [
                shutil.which("env"),
                "PATH=" + command_path(root, case),
                *arguments,
            ]
        terminal.spawn(*arguments)
        if case == "tab0.sh":
            terminal.input(b"\n" * 16)
        deadline = time.monotonic() + 1.25
        status = None
        while time.monotonic() < deadline:
            status, _ = terminal.poll_child()
            if status is not None:
                break
            time.sleep(0.01)
        after = observable(terminal)
    if status not in (None, 0):
        return f"child exited {status}"
    if after == before:
        return "scenario produced no observable terminal state"
    return ""


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
    if case in PREFIX_CASES:
        message = "chunking changed state"
        payload = generate_prefix(root, case)
        with Zutty(columns=80, rows=25, save_lines=500) as whole, \
             Zutty(columns=80, rows=25, save_lines=500) as chunked:
            whole.write(payload)
            write_chunked(chunked, payload)
            mismatch = observable(whole) != observable(chunked)
    elif case in LIVE_CASES or case in PERL_LIVE_CASES:
        message = run_live_case(root, case)
        mismatch = bool(message)
        payload = b""
    elif case == "doublechars.sh":
        message = run_pty_case(root, case)
        mismatch = bool(message)
        payload = b""
    else:
        message = "chunking changed state"
        if case.endswith(".pl") and case not in PERL_GENERATOR_CASES:
            raise RuntimeError(f"unclassified xterm Perl scenario: {case}")
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
        print(f"FAIL xterm-vttests/{case}: {message}", file=sys.stderr)
        return 1
    else:
        detail = (
            "live PTY scenario"
            if case in LIVE_CASES or case in PERL_LIVE_CASES
            else f"{len(payload)}-byte live stream prefix"
            if case in PREFIX_CASES
            else "PTY scenario"
            if case == "doublechars.sh"
            else f"{len(payload)} stream bytes"
        )
        print(f"PASS xterm-vttests/{case}: {detail}")
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
