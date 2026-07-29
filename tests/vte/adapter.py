#!/usr/bin/env python3

import itertools
import os
import signal
import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Shitty


PARAMETER_SETS = (
    (-1, 0, 1, 9, 10, 99, 100, 999,
     1000, 9999, 10000, 65534, 65535, 65536, -1, -1),
    (1, -1, -1, -1, 1, -1, 1, 1,
     1, -1, -1, -1, -1, 1, 1, 1),
)
SPECIAL_ESC_FINALS = b"PXZ[]^_"
BATCH_SIZE = 4096
VTE_CONTROLS = (
    *range(0x00, 0x18),
    0x19,
    0x1a,
    *range(0x1c, 0x20),
    *range(0x82, 0x90),
    *range(0x91, 0x98),
    0x9c,
)


def params(values):
    return ";".join("" if value < 0 else str(min(value, 65535))
                    for value in values).encode()


def canonical_params(value):
    if not value:
        return b""
    result = bytearray()
    token = bytearray()
    for byte in value:
        if byte not in b";:":
            token.append(byte)
            continue
        result.extend(token or b"0")
        result.append(byte)
        token.clear()
    result.extend(token or b"0")
    return bytes(result)


def csi_sequence(prefix, values, intermediate, final, c1):
    arguments = params(values)
    introducer = b"\x9b" if c1 else b"\x1b["
    sequence = introducer + prefix + arguments + intermediate + bytes((final,))
    data = prefix + canonical_params(arguments) + intermediate + bytes((final,))
    return sequence, ("csi", data)


def dcs_sequence(prefix, values, intermediate, final, body, c1):
    arguments = params(values)
    introducer = b"\x90" if c1 else b"\x1bP"
    terminator = b"\x9c" if c1 else b"\x1b\\"
    sequence = introducer + prefix + arguments + intermediate + bytes((final,)) + body + terminator
    data = prefix + arguments + intermediate + bytes((final,)) + body
    return sequence, ("dcs", data)


def summarize(events):
    return [(event, data[:80], len(data)) for event, data in events]


def run_batches(items, batch_size=BATCH_SIZE, single_byte=False):
    checked = 0
    first_mismatch = None
    with Shitty(columns=5, rows=5, save_lines=5) as terminal:
        if single_byte:
            terminal.write(b"\x1b%@")
        terminal.parser_trace_on()
        iterator = iter(items)
        while True:
            batch = list(itertools.islice(iterator, batch_size))
            if not batch:
                break
            prefix = b"\x1b%@" if single_byte else b""
            terminal.write(b"".join(prefix + sequence for sequence, _ in batch))
            actual = terminal.parser_trace()
            if single_byte:
                actual = [event for event in actual if event != ("escape", b"%@")]
            expected = [event for _, event in batch if event is not None]
            if actual != expected:
                limit = min(len(actual), len(expected))
                index = next((n for n in range(limit)
                              if actual[n] != expected[n]), limit)
                absolute = checked + index
                mismatch = (
                    f"event {absolute}: got {summarize(actual[index:index + 3])!r}, "
                    f"expected {summarize(expected[index:index + 3])!r}; "
                    f"batch totals {len(actual)}/{len(expected)}"
                )
                if first_mismatch is None:
                    first_mismatch = mismatch
            checked += len(batch)
    return checked, first_mismatch


def run_isolated(items, single_byte=False):
    checked = 0
    first_mismatch = None
    for sequence, expected in items:
        with Shitty(columns=5, rows=5, save_lines=5) as terminal:
            if single_byte:
                terminal.write(b"\x1b%@")
            terminal.parser_trace_on()
            terminal.write(sequence)
            actual = terminal.parser_trace()
        wanted = [] if expected is None else [expected]
        if actual != wanted and first_mismatch is None:
            first_mismatch = (f"case {checked}: got {summarize(actual)!r}, "
                              f"expected {summarize(wanted)!r}")
        checked += 1
    return checked, first_mismatch


def controls():
    for control in VTE_CONTROLS:
        yield bytes((control,)), (
            None if control == 0 else ("control", bytes((control,)))
        )


def run_escape_invalid():
    checked = 0
    first_mismatch = None
    with Shitty(columns=5, rows=5, save_lines=5) as terminal:
        terminal.parser_trace_on()
        for chunked in (False, True):
            for final in range(0x20):
                terminal.parser_trace_clear()
                if chunked:
                    terminal.write(b"\x1b")
                    terminal.write(bytes((final,)))
                else:
                    terminal.write(b"\x1b" + bytes((final,)))
                actual = terminal.parser_trace()
                if any(event == "escape" for event, _ in actual) and first_mismatch is None:
                    first_mismatch = (
                        f"ESC {final:#04x}, chunked={chunked}: "
                        f"unexpected {summarize(actual)!r}"
                    )
                terminal.write(b"\x18")
                checked += 1
    return checked, first_mismatch


def csi_clear_source():
    arguments = b";".join(str(127 * index + 17).encode() for index in range(16))
    return b"\x1b[?" + arguments + b"m"


def run_csi_clear():
    checked = 0
    first_mismatch = None
    source = csi_clear_source()
    with Shitty(columns=5, rows=5, save_lines=5) as terminal:
        terminal.parser_trace_on()
        for length in range(1, len(source) + 1):
            for argument_count in range(16):
                terminal.write(source[:length])
                terminal.parser_trace_clear()
                arguments = b";".join(
                    str(257 * index + 31).encode()
                    for index in range(argument_count)
                )
                sequence = b"\x1b[>" + arguments + b"n"
                terminal.write(sequence)
                actual = terminal.parser_trace()
                expected = [("csi", b">" + arguments + b"n")]
                if actual != expected and first_mismatch is None:
                    first_mismatch = (
                        f"prefix length {length}, arguments {argument_count}: "
                        f"got {summarize(actual)!r}, "
                        f"expected {summarize(expected)!r}"
                    )
                checked += 1
    return checked, first_mismatch


def escape_nf():
    boundary_finals = (0x30, 0x3f, 0x40, 0x4f, 0x50, 0x5f, 0x60, 0x7e)
    boundary_intermediates = (0x20, 0x21, 0x27, 0x28, 0x2e, 0x2f)
    for final in range(0x30, 0x7f):
        for depth in range(3):
            for intermediate in itertools.product(range(0x20, 0x30), repeat=depth):
                data = bytes(intermediate) + bytes((final,))
                sequence = b"\x1b" + data
                if depth == 0 and final in SPECIAL_ESC_FINALS:
                    yield sequence + b"\x18", ("control", b"\x18")
                else:
                    yield sequence, ("escape", data)
    for depth, alphabet in ((3, range(0x20, 0x30)), (4, boundary_intermediates)):
        for final in boundary_finals:
            for intermediate in itertools.product(alphabet, repeat=depth):
                data = bytes(intermediate) + bytes((final,))
                yield b"\x1b" + data, ("escape", data)


def escape_fpes():
    for final in range(0x30, 0x7f):
        sequence = b"\x1b" + bytes((final,))
        if final in SPECIAL_ESC_FINALS:
            yield sequence + b"\x18", ("control", b"\x18")
        else:
            yield sequence, ("escape", bytes((final,)))


def csi_matrix():
    for values in PARAMETER_SETS:
        for prefix in (b"", b"<", b"=", b">", b"?"):
            for final in range(0x40, 0x7f):
                for count in range(17):
                    for c1 in (False, True):
                        yield csi_sequence(prefix, values[:count], b"", final, c1)
                for intermediate in range(0x20, 0x30):
                    for count in (0, 8, 16):
                        for c1 in (False, True):
                            yield csi_sequence(prefix, values[:count], bytes((intermediate,)), final, c1)
            for final in (0x40, 0x4f, 0x50, 0x5f, 0x60, 0x6f, 0x70, 0x7e):
                for intermediate in itertools.product(range(0x20, 0x30), repeat=2):
                    for count in (0, 1, 8, 16):
                        for c1 in (False, True):
                            yield csi_sequence(prefix, values[:count], bytes(intermediate), final, c1)


def csi_parameters():
    values = (b"", b";", b":", b";:", b"::;;",
              b"1;2:3:4:5:6;7:8;9:0",
              b"1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1",
              b"1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1")
    for value in values:
        yield b"\x9b" + value + b"m", ("csi", canonical_params(value) + b"m")


def csi_max():
    base = b"?" + b";".join(str(127 * n + 17).encode() for n in range(32))
    yield b"\x1b[" + base + b"m", ("csi", base + b"m")
    for more in (b":", b";", b":12345", b";12345",
                 b":12345;", b";12345:", b":12345;", b":12345:"):
        sequence = b"\x1b[" + base + more + b"m\x18"
        yield sequence, ("control", b"\x18")


def csi_misc():
    cases = (
        b"\x1b[\xc4\x80a", b"\x9b\xc4\x80a",
        b"\x1b[1\xc4\x80a", b"\x9b1\xc4\x80a",
        b"\x1b[1 \xc4\x80a", b"\x9b1 \xc4\x80a",
        b"\x1b[?1 \xc4\x80a", b"\x9b?1 \xc4\x80a",
    )
    for sequence in cases:
        yield sequence, ("text", "Āa".encode())


def dcs_matrix():
    body = b"123;TESTING"
    for values in PARAMETER_SETS:
        for prefix in (b"", b"<", b"=", b">", b"?"):
            for final in range(0x40, 0x7f):
                for count in range(17):
                    for c1 in (False, True):
                        yield dcs_sequence(prefix, values[:count], b"", final, body, c1)
                for intermediate in range(0x20, 0x30):
                    for count in (0, 8, 16):
                        for c1 in (False, True):
                            yield dcs_sequence(prefix, values[:count], bytes((intermediate,)),
                                               final, body, c1)
            for final in (0x40, 0x4f, 0x50, 0x5f, 0x60, 0x6f, 0x70, 0x7e):
                for intermediate in itertools.product(range(0x20, 0x30), repeat=2):
                    for count in (0, 1, 8, 16):
                        for c1 in (False, True):
                            yield dcs_sequence(prefix, values[:count], bytes(intermediate),
                                               final, body, c1)


def dcs_misc():
    invalid = (
        b"\x1bP\xc4\x80a\x1b\\", b"\x90\xc4\x80a\x1b\\",
        b"\x1bP1\xc4\x80a\x1b\\", b"\x901\xc4\x80a\x1b\\",
        b"\x1bP1 \xc4\x80a\x1b\\", b"\x901 \xc4\x80a\x1b\\",
        b"\x1bP?1 \xc4\x80a\x1b\\", b"\x90?1 \xc4\x80a\x1b\\",
    )
    for sequence in invalid:
        yield sequence + b"x", ("text", b"x")
    for sequence, event in (
        (b"\x1b\\", ("escape", b"\\")),
        (b"\x9c", ("control", b"\x9c")),
        (b"\x1b\x1b\\", ("escape", b"\\")),
        (b"\x1b\x9c", ("control", b"\x9c")),
    ):
        yield sequence, event


def osc_lengths():
    for length in range(4096):
        body = bytes((0x20 + length % 95,)) * length
        yield b"\x1b]" + body + b"\x1b\\", ("osc", body)


def osc_oversize():
    body = chr(0x100000).encode() * 4097
    yield b"\x1b]" + body + b"\x1b\\x", ("text", b"x")


def osc_controls(name):
    _, _, c1, introducer_name, terminator_name = name.split("_")
    c1 = bool(int(c1))
    default_introducer = b"\x9d" if c1 else b"\x1b]"
    introducer = {"default": default_introducer, "c0": b"\x1b]", "c1": b"\x9d"}[introducer_name]
    default_terminator = b"\x9c" if c1 else b"\x1b\\"
    terminator = {"default": default_terminator, "c0": b"\x1b\\",
                  "c1": b"\x9c", "bel": b"\x07"}[terminator_name]
    resolved_introducer = ("c1" if c1 else "c0") if introducer_name == "default" else introducer_name
    resolved_terminator = ("c1" if c1 else "c0") if terminator_name == "default" else terminator_name
    accepted = ((resolved_introducer == "c0" and resolved_terminator in ("c0", "bel"))
                or (resolved_introducer == "c1" and resolved_terminator == "c1"))
    sequence = introducer + b"TEST" + terminator
    if accepted:
        yield sequence, ("osc", b"TEST")
    else:
        yield sequence + b"\x18", ("control", b"\x18")


def items(name):
    if name == "controls":
        return controls()
    if name == "escape_nf":
        return escape_nf()
    if name == "escape_fpes":
        return escape_fpes()
    if name == "csi":
        return csi_matrix()
    if name == "csi_parameters":
        return csi_parameters()
    if name == "csi_max":
        return csi_max()
    if name == "csi_misc":
        return csi_misc()
    if name == "dcs":
        return dcs_matrix()
    if name == "dcs_misc":
        return dcs_misc()
    if name == "osc_lengths":
        return osc_lengths()
    if name == "osc_oversize":
        return osc_oversize()
    if name.startswith("osc_controls_"):
        return osc_controls(name)
    raise KeyError(name)


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: adapter.py CASE XFAIL_FILE STAMP")
    name = sys.argv[1]
    known = {
        line.strip() for line in Path(sys.argv[2]).read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }
    timeout_message = f"FAIL VTE/{name}: exceeded 30 seconds\n".encode()
    def timed_out(_signum, _frame):
        os.write(2, timeout_message)
        os._exit(124)
    signal.signal(signal.SIGALRM, timed_out)
    signal.alarm(30)
    isolated = name in {
        "csi_max", "csi_misc", "dcs_misc", "osc_oversize",
    } or name.startswith("osc_controls_")
    single_byte = name in {
        "controls", "csi", "csi_parameters", "dcs", "dcs_misc",
    } or name.startswith("osc_controls_")
    if name == "escape_invalid":
        checked, mismatch = run_escape_invalid()
    elif name == "csi_clear":
        checked, mismatch = run_csi_clear()
    elif isolated:
        checked, mismatch = run_isolated(items(name), single_byte=single_byte)
    else:
        checked, mismatch = run_batches(
            items(name),
            64 if name == "osc_lengths" else BATCH_SIZE,
            single_byte=single_byte,
        )
    signal.alarm(0)
    if (mismatch is not None) != (name in known):
        status = "FAIL" if mismatch is not None else "XPASS"
        print(f"{status} VTE/{name}: {mismatch or 'matched'}", file=sys.stderr)
        return 1
    print(f"{'XFAIL' if mismatch else 'PASS'} VTE/{name}: {checked} sequences"
          + (f"; {mismatch}" if mismatch else ""))
    stamp = Path(sys.argv[3])
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
