#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import re
from pathlib import Path

from screen_catalog import call_end


SOURCE = Path(__file__).resolve().parent / "upstream" / "mod.rs"
ASSERTION = re.compile(r"\bterm\.assert_dirty_lines\s*\(")


def write(payload):
    return ("write", payload)


def source_assertions():
    text = SOURCE.read_text()
    result = []
    for match in ASSERTION.finditer(text):
        opening = text.index("(", match.start())
        call = text[opening + 1:call_end(text, opening) - 1]
        expected = re.search(r"&\[\s*([0-9,\s]*)\]", call)
        if expected is None:
            raise ValueError("unrecognized WezTerm dirty-line assertion")
        values = tuple(map(int, re.findall(r"\d+", expected.group(1))))
        line = text.count("\n", 0, match.start()) + 1
        result.append((f"mod_{line:04d}", values))
    return tuple(result)


def damage_cases():
    cases = {}

    def add(name, label, rows, columns, save_lines, actions, upstream, visible):
        cases[name] = (
            name,
            label,
            rows,
            columns,
            save_lines,
            tuple(actions),
            upstream,
            visible,
        )

    cursor = [write(b"fooo.")]
    add("mod_0546", "cursor_movement_damage", 2, 3, 0, cursor, (0, 1), (0, 1))
    cursor.append(write(b"\x1b[2;1H"))
    cursor.append(write(b"\x08"))
    add("mod_0554", "cursor_movement_damage", 2, 3, 0, cursor, (), ())
    cursor.append(write(b"\x1b[1;1H"))
    add("mod_0558", "cursor_movement_damage", 2, 3, 0, cursor, (), ())

    deleted = [write(b"111\r\n222\r\n333\r\n444\r\n555")]
    add(
        "mod_0679", "test_delete_lines", 5, 3, 0, deleted,
        (0, 1, 2, 3, 4), (0, 1, 2, 3, 4),
    )
    deleted.append(write(b"\x1b[2;1H"))
    add("mod_0683", "test_delete_lines", 5, 3, 0, deleted, (), ())
    deleted.append(write(b"\x1b[2M"))
    add(
        "mod_0686", "test_delete_lines", 5, 3, 0, deleted,
        (1, 2, 3, 4), (1, 2, 3, 4),
    )
    deleted.append(write(b"\x1b[4;1Haaa\r\nbbb\x1b[2;1H"))
    deleted.append(write(b"\x1b[2;4r\x1b[2;1H"))
    deleted.append(write(b"\x1b[2M"))
    add(
        "mod_0707", "test_delete_lines", 5, 3, 0, deleted,
        (1, 2, 3), (1, 2, 3),
    )
    deleted.append(write(b"\x1b[1;5r"))
    deleted.append(write(b"\x1b[M"))
    add(
        "mod_0717", "test_delete_lines", 5, 3, 0, deleted,
        (4,), (0, 1, 2, 3, 4),
    )

    region = [write(b"1\n2\n3\n4\n5")]
    region.append(write(b"\x1b[2;3r\x1b[3;1H"))
    region.append(write(b"\na"))
    add("mod_1200", "test_region_scroll", 5, 1, 10, region, (1, 2), (1, 2))
    region.append(write(b"\x1b[1;2r\x1b[2;1H"))
    region.append(write(b"\nb"))
    add(
        "mod_1212", "test_region_scroll", 5, 1, 10, region,
        (2, 3, 4, 5), (0, 1),
    )
    region.append(write(b"\x1b[2;1H"))
    region.append(write(b"\x1b[3M"))
    add("mod_1221", "test_region_scroll", 5, 1, 10, region, (2,), (1,))
    region.append(write(b"\x1b[1;5r\x1b[5;1H"))
    region.append(write(b"\nX"))
    add(
        "mod_1231", "test_region_scroll", 5, 1, 10, region,
        (6,), (0, 1, 2, 3, 4),
    )

    alternate = [write(b"M\no\nn\nk\ne\ny")]
    alternate.append(write(b"\x1b[?1049h1\n2\n3\n4\n5"))
    alternate.append(write(b"\x1b[2;3r\x1b[3;1H"))
    alternate.append(write(b"\na"))
    add(
        "mod_1252", "test_alt_screen_region_scroll", 5, 1, 10,
        alternate, (1, 2), (1, 2),
    )
    alternate.append(write(b"\x1b[1;2r\x1b[2;1H"))
    alternate.append(write(b"\nb"))
    add(
        "mod_1261", "test_alt_screen_region_scroll", 5, 1, 10,
        alternate, (0, 1), (0, 1),
    )
    alternate.append(write(b"\x1b[1;5r\x1b[5;1H"))
    alternate.append(write(b"\nX"))
    add(
        "mod_1271", "test_alt_screen_region_scroll", 5, 1, 10,
        alternate, (0, 1, 2, 3, 4), (0, 1, 2, 3, 4),
    )
    alternate.append(write(b"\x1b[?1049l"))
    add(
        "mod_1278", "test_alt_screen_region_scroll", 5, 1, 10,
        alternate, (0, 1, 2, 3, 4), (0, 1, 2, 3, 4),
    )

    limited = [
        write(b"1\n2\n3\n4"),
        write(b"\x1b[1;2r\x1b[2;1H"),
        write(b"A\nB\nC\nD"),
    ]
    add(
        "mod_1293", "test_region_scrollback_limit", 4, 1, 2,
        limited, (0, 1, 2, 3, 4, 5), (0, 1),
    )

    for name, upstream in source_assertions():
        case = cases.get(name)
        if case is None:
            raise ValueError(f"untranslated WezTerm damage assertion {name}")
        if case[-2] != upstream:
            raise ValueError(
                f"WezTerm damage oracle mismatch for {name}: "
                f"source={upstream}, translated={case[-2]}"
            )
        yield case


CASES = tuple(damage_cases())


def case_names():
    return tuple(case[0] for case in CASES)


def case_data(name):
    for case in CASES:
        if case[0] == name:
            return case[1:]
    raise KeyError(name)
