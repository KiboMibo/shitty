#!/usr/bin/env python3

import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Shitty
from utf8_catalog import utf8_cases


def as_bytes(value):
    return value if isinstance(value, bytes) else value.encode()


def decode(terminal, chunks):
    terminal.utf8_reset()
    codepoints = []
    for chunk in chunks:
        codepoints.extend(terminal.utf8_push(chunk))
    codepoints.extend(terminal.utf8_flush())
    return "".join(map(chr, codepoints))


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: utf8_adapter.py STAMP")
    cases = tuple(utf8_cases())
    if len(cases) != 313:
        raise RuntimeError(
            f"expected 313 Kitty UTF-8 rows, found {len(cases)}"
        )

    with Shitty(columns=5, rows=5, save_lines=0) as terminal:
        for line, source, expected in cases:
            actual = decode(terminal, (b"filler" + as_bytes(source),))
            if actual != "filler" + expected:
                raise RuntimeError(
                    f"Kitty UTF-8 line {line}: "
                    f"{actual!r} != {expected!r}"
                )

        for prefix in (b"abcd", "😸".encode()):
            for suffix in (b"1234", "😸".encode()):
                for first, second in (
                    (prefix + b"\xf0\x9f", b"\x98\xb8" + suffix),
                    (prefix + b"\xf0\x9f\x9b", b"\xb8" + suffix),
                    (prefix + b"\xf0", b"\x9f\x98\xb8" + suffix),
                    (prefix + b"\xc3", b"\xa4" + suffix),
                    (prefix + b"\xe2", b"\x89\xa4" + suffix),
                    (prefix + b"\xe2\x89", b"\xa4" + suffix),
                ):
                    actual = decode(terminal, (first, second))
                    expected = (first + second).decode()
                    if actual != expected:
                        raise RuntimeError(
                            "Kitty split UTF-8: "
                            f"{actual!r} != {expected!r}"
                        )

    print(
        f"PASS Kitty UTF-8: {len(cases)} oracle rows "
        "and 24 split sequences"
    )
    stamp = Path(sys.argv[1])
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
