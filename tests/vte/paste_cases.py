#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.


CASE_NAMES = (
    "brackets-c0",
    "controls-c0",
    "controls-c1",
    "strings",
)

C0_CONTROLS = tuple(
    value
    for value in range(0x20)
    if value not in (0x00, 0x09, 0x0a, 0x0d)
) + (0x7f,)

C1_CONTROLS = tuple(range(0x80, 0xa0))

STRING_CASES = (
    (b"\x09", b"\x09"),
    (b"\x0d", b"\x0d"),
    (b"abc\xc2\xa0xyz", b"abc\xc2\xa0xyz"),
    (b"\x0a", b"\x0d"),
    (b"\x0a\x0d", b"\x0d\x0d"),
    (b"\x0d\x0a", b"\x0d"),
    (b"\x0d\x0a\x0d", b"\x0d\x0d"),
    (b"\x0d\x0a\x0d\x0a", b"\x0d\x0d"),
)


def control_vectors(control, picture):
    return (
        (control, picture),
        (control + control, picture + picture),
        (b"abc" + control, b"abc" + picture),
        (b"abc" + control + control, b"abc" + picture + picture),
        (control + b"abc", picture + b"abc"),
        (control + control + b"abc", picture + picture + b"abc"),
        (b"abc" + control + b"abc", b"abc" + picture + b"abc"),
        (
            b"abc" + control + control + b"abc",
            b"abc" + picture + picture + b"abc",
        ),
    )


def control_picture(value):
    if value == 0x7f:
        return b"\xe2\x90\xa1"
    if value >= 0x80:
        return b"\xef\xbf\xbd"
    return bytes((0xe2, 0x90, value + 0x80))
