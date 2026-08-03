#!/usr/bin/env python3

import os
import signal
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Shitty


LIVE_CASES = {
    "acolors.sh",
    "dynamic.sh",
    "dynamic2.sh",
    "fonts.sh",
    "tab0.sh",
}

TRANSLATED_CASES = {
    "resize.sh",
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
    "utf8.pl",
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
    "lrmm-scroll.pl",
    "palettes.pl",
    "paste64.pl",
    "report-sgr.pl",
    "titlestack.pl",
    "under-latin.pl",
    "xorblink.pl",
    "xtra-scroll.pl",
}

PERL_INTERACTIVE_CASES = {
    "cursor.pl",
    "lrmm-scroll.pl",
    "palettes.pl",
    "paste64.pl",
    "report-sgr.pl",
    "titlestack.pl",
    "under-latin.pl",
    "xorblink.pl",
    "xtra-scroll.pl",
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
        elif case == "utf8.pl":
            arguments.extend((
                "0x20", "0x41", "0xa1", "0x301", "0x3a9", "0x20ac",
                "0x2500", "0x754c", "0xff01", "0x1f642", "0x1f680",
            ))
        elif case == "resize.pl":
            # resize.pl loops forever; no watchdog is needed - the
            # adapter observes for its fixed window and the terminal's
            # QUIT handler SIGKILLs a still-running child on close.
            pass
        elif case == "cursor.pl":
            # cursor.pl is a viewer: without an input file it only clears an
            # already empty screen, so the scenario has no observable oracle.
            arguments.append(script)
        return arguments
    if case.endswith(".sh"):
        bash = shutil.which("bash")
        if bash is None:
            raise RuntimeError("xterm shell scenarios require bash")
        return [bash, script]
    return [script]


def command_path(root, case):
    path = str(root / "bin") + os.pathsep + os.environ["PATH"]
    if case.endswith(".pl"):
        path = str(Path(command_for(root, case)[0]).parent) + os.pathsep + path
    return path


def perl_library_path(root):
    result = str(root / "lib")
    if os.environ.get("PERL5LIB"):
        result += os.pathsep + os.environ["PERL5LIB"]
    return result


def observable(terminal):
    return (
        terminal.read_input(),
        terminal.read_actions(),
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
    environment["PERL5LIB"] = perl_library_path(root)
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
    source = (root / "upstream" / case).read_text()
    echo_assignment = "CMD='/bin/echo'"
    option_assignment = "OPT='-n'"
    if (
        source.count(echo_assignment) != 1
        or source.count(option_assignment) != 1
    ):
        raise RuntimeError(f"{case} has unexpected echo configuration")
    # These unbounded generators emit every short fragment through
    # /bin/echo. Running the same byte stream through the shell builtin
    # avoids tens of thousands of fork/exec calls while keeping the
    # preserved upstream source untouched.
    source = source.replace(echo_assignment, "CMD='printf %s'", 1)
    source = source.replace(option_assignment, "OPT=''", 1)
    shell = shutil.which("sh")
    if shell is None:
        raise RuntimeError("xterm prefix scenarios require sh")
    # The selected upstream scripts are unbounded generators. Keep stderr out
    # of a pipe so a noisy failure cannot deadlock stdout collection before we
    # have read the requested prefix.
    with tempfile.TemporaryFile() as errors:
        process = subprocess.Popen(
            [shell, "-c", source],
            stdout=subprocess.PIPE,
            stderr=errors,
            env=environment,
        )
        try:
            payload = process.stdout.read(limit)
        finally:
            process.kill()
            process.wait(timeout=5)
        if len(payload) != limit:
            errors.seek(0)
            detail = errors.read(16 * 1024).decode(errors="replace").strip()
            message = (
                f"{case} ended before producing a {limit}-byte stream prefix"
            )
            if detail:
                message += ": " + detail
            raise RuntimeError(message)
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
    with Shitty(columns=columns, rows=rows, save_lines=500) as terminal:
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
    with Shitty(
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
                "PERL5LIB=" + perl_library_path(root),
                *arguments,
            ]
        terminal.spawn(*arguments)
        if case == "tab0.sh":
            terminal.input(b"\n" * 16)
        deadline = time.monotonic() + 1.25
        input_deadline = time.monotonic() + 0.25
        sent_input = False
        status = None
        screen = ""
        while time.monotonic() < deadline:
            status, screen = terminal.poll_child()
            if status is not None:
                break
            if (
                case in PERL_INTERACTIVE_CASES
                and not sent_input
                and time.monotonic() >= input_deadline
            ):
                terminal.input(b"q\n" * 64)
                sent_input = True
            time.sleep(0.01)
        after = observable(terminal)
    if status not in (None, 0):
        detail = screen.strip().splitlines()[-1] if screen.strip() else ""
        return f"child exited {status}" + (f": {detail}" if detail else "")
    if after == before:
        return "scenario produced no observable terminal state"
    return ""


def run_translated_case(case):
    with Shitty(
        columns=80,
        rows=25,
        save_lines=500,
        extra_arguments=("-allowWindowOps", "true"),
    ) as terminal:
        if case == "resize.sh":
            terminal.write(b"\x1b[18t\x1b[19t")
            if terminal.read_input() != (
                b"\x1b[8;25;80t\x1b[9;1076;1916t"
            ):
                return "current or maximum geometry report is incorrect"
            terminal.write(b"\x1b[8;26;81t")
            if terminal.read_actions() != ["WINDOW 8 26 81"]:
                return "resize request did not reach the window backend"
            return ""

        if case == "title.sh":
            terminal.write(b"\x1b[21t")
            original = terminal.read_input()
            if original != b"\x1b]lShitty\x1b\\":
                return "initial window title report is incorrect"
            terminal.write(
                b"\x1b]2;Mon Jul 29 12:34:56 UTC 2026\x07"
                b"\x1b[21t"
            )
            if terminal.read_input() != (
                b"\x1b]lMon Jul 29 12:34:56 UTC 2026\x1b\\"
            ):
                return "clock title update or report is incorrect"
            terminal.write(b"\x1b]2;Shitty\x07\x1b[21t")
            if terminal.read_input() != original:
                return "original window title was not restored"
            return ""

        if case == "version.sh":
            terminal.write(b"\x1b[>0q")
            response = terminal.read_input()
            if not (
                response.startswith(b"\x1bP>|Shitty ")
                and response.endswith(b"\x1b\\")
                and len(response) > len(b"\x1bP>|Shitty \x1b\\")
            ):
                return "version response is malformed"
            return ""

    raise RuntimeError(f"unclassified translated scenario: {case}")


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
    # Prefix cases feed 256 KiB through two independently driven terminals.
    # Leave enough headroom for slower debug/Nix builders.
    timeout = 60 if case in PREFIX_CASES else 20
    if os.environ.get("ASAN_OPTIONS"):
        timeout *= 3
    signal.alarm(timeout)
    if case in PREFIX_CASES:
        message = "chunking changed state"
        payload = generate_prefix(root, case)
        with Shitty(columns=80, rows=25, save_lines=500) as whole, \
             Shitty(columns=80, rows=25, save_lines=500) as chunked:
            whole.write(payload)
            write_chunked(chunked, payload)
            mismatch = observable(whole) != observable(chunked)
    elif case in TRANSLATED_CASES:
        message = run_translated_case(case)
        mismatch = bool(message)
        payload = b""
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
        with Shitty(columns=80, rows=25, save_lines=500) as whole, \
             Shitty(columns=80, rows=25, save_lines=500) as chunked:
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
            "translated finite protocol scenario"
            if case in TRANSLATED_CASES
            else "live PTY scenario"
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
