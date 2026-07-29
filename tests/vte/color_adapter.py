#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from color_cases import case_names, named_colors
from harness import Shitty


def reply(color):
    red, green, blue = color
    return (
        b"\x1b]12;rgb:"
        + f"{red:02x}{red:02x}/{green:02x}{green:02x}/"
          f"{blue:02x}{blue:02x}".encode()
        + b"\x1b\\"
    )


def query(terminal):
    terminal.write(b"\x1b]12;?\x1b\\")
    return terminal.read_input()


def set_color(terminal, spec):
    terminal.write(b"\x1b]12;" + spec.encode() + b"\x1b\\")


def assert_valid(terminal, spec, color):
    set_color(terminal, spec)
    actual = query(terminal)
    expected = reply(color)
    if actual != expected:
        raise AssertionError(
            f"{spec!r}: expected {expected!r}, got {actual!r}"
        )


def assert_invalid(terminal, spec):
    before = query(terminal)
    set_color(terminal, spec)
    after = query(terminal)
    if after != before:
        raise AssertionError(
            f"{spec!r}: invalid XParseColor value changed "
            f"{before!r} to {after!r}"
        )


def run_parse_css():
    vectors = (
        ("rgba(100,90,80,0.1)", None),
        ("rgba(40%,30%,20%,0.1)", None),
        ("rgba(  40 % ,  30 %  ,   20 % ,  0.1    )", None),
        ("red", (255, 0, 0)),
        ("#0080ff", (0, 128, 255)),
        ("#0080ff80", None),
        ("rgb(0,0,0)", None),
        ("hsl (0, 100%, 50%)", None),
        ("hsla (120, 255, 50%, 0.1)", None),
        ("hsl(180, 100%, 25%)", None),
        ("hsl(360, 100, 25)", None),
    )
    with Shitty() as terminal:
        for spec, color in vectors:
            if color is None:
                assert_invalid(terminal, spec)
            else:
                assert_valid(terminal, spec, color)


def run_parse_x11():
    vectors = (
        ("#789", (0x70, 0x80, 0x90)),
        ("#78899a", (0x78, 0x89, 0x9a)),
        ("#7899abbcd", (0x78, 0x9a, 0xbc)),
        ("#789a9abcbcde", (0x78, 0x9a, 0xbc)),
        ("rgb:7/8/9", (0x77, 0x88, 0x99)),
        ("rgb:78/89/9a", (0x78, 0x89, 0x9a)),
        ("rgb:789/9ab/bcd", (0x78, 0x9a, 0xbc)),
        ("rgb:789a/9abc/bcde", (0x78, 0x9a, 0xbc)),
    )
    with Shitty() as terminal:
        for spec, color in vectors:
            assert_valid(terminal, spec, color)


def run_parse_named():
    colors = named_colors()
    if len(colors) != 782:
        raise AssertionError(f"expected 782 VTE named colors, got {len(colors)}")
    with Shitty() as terminal:
        for spec, packed in colors:
            assert_valid(
                terminal,
                spec,
                (packed >> 16, (packed >> 8) & 0xff, packed & 0xff),
            )


def run_parse_nothing():
    invalid = (
        "",
        "foo",
        "rgba(100,90,80,0.1)",
        "rgb(,,)",
        "rgb(%,%,%)",
        "rgb(nan,nan,nan)",
        "rgb(inf,inf,inf)",
        "rgb(1p12,0,0)",
        "rgb(5d1%,1,1)",
        "rgb(0,0,0)foo",
        "rgb(0,0,0)  foo",
        "#XGB",
        "#XGBQ",
        "#AAAAXGBQ",
        "rgb:00000/000000/000000",
    )
    with Shitty() as terminal:
        for spec in invalid:
            assert_invalid(terminal, spec)

        # These two are rejected only by the opposite VTE parser mode.
        # Both are valid XParseColor values in a terminal OSC.
        assert_valid(terminal, "rgb:00/00/00", (0, 0, 0))
        assert_valid(terminal, "rgbi:0.0/0.0/0.0", (0, 0, 0))


def run_to_string():
    with Shitty() as terminal:
        for spec, color in (
            ("#000000", (0, 0, 0)),
            ("#123456", (0x12, 0x34, 0x56)),
            ("#ffffff", (255, 255, 255)),
        ):
            assert_valid(terminal, spec, color)

        for spec in ("#00000000", "#12345678", "#ffffffff"):
            assert_invalid(terminal, spec)


RUNNERS = {
    "parse-css": run_parse_css,
    "parse-x11": run_parse_x11,
    "parse-named": run_parse_named,
    "parse-nothing": run_parse_nothing,
    "to-string": run_to_string,
}


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: color_adapter.py CASE STAMP")
    name = sys.argv[1]
    stamp = Path(sys.argv[2])
    if name not in case_names():
        raise SystemExit(f"unknown VTE color case: {name}")
    RUNNERS[name]()
    print(f"PASS VTE color/{name}")
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
