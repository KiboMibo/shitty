#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import re
from pathlib import Path

from catalog import decode_rust
from screen_catalog import call_end


ROOT = Path(__file__).resolve().parent
SOURCES = tuple(
    ROOT / "upstream" / name
    for name in ("csi.rs", "mod.rs", "selection.rs")
)
ASSERTION = re.compile(r"\bassert_all_contents\s*\(")
STRING = re.compile(r'b?("(?:\\.|[^"\\])*")')
VALUES = {
    "waving_hand": "\U0001f44b",
    "waving_hand_dark_tone": "\U0001f44b\U0001f3ff",
    "sequence": "\u1112\u1161\u11ab",
}
STABLE = re.compile(
    r"assert_eq!\(\s*term\.screen\(\)\.visible_row_to_stable_row"
    r"\(\s*(\d+)\s*\)\s*,\s*(\d+)\s*\)"
)


def source_assertions():
    result = []
    for source in SOURCES:
        text = source.read_text()
        for match in ASSERTION.finditer(text):
            if source.name == "mod.rs" and text.count("\n", 0, match.start()) + 1 == 309:
                continue
            opening = text.index("(", match.start())
            call = text[opening + 1:call_end(text, opening) - 1]
            array = re.search(r"&\[(.*)\]\s*,?\s*$", call, re.DOTALL)
            if array is None:
                raise ValueError("unrecognized WezTerm all-contents assertion")
            expected = []
            for part in array.group(1).split(","):
                part = part.strip()
                if not part:
                    continue
                string = STRING.fullmatch(part)
                if string is not None:
                    expected.append(decode_rust(string.group(1)).decode("utf-8"))
                elif part in VALUES:
                    expected.append(VALUES[part])
                else:
                    raise ValueError(
                        f"unrecognized WezTerm all-contents value {part!r}"
                    )
            line = text.count("\n", 0, match.start()) + 1
            result.append((f"{source.stem}_{line:04d}", tuple(expected)))
    return tuple(result)


def source_stable_assertions():
    text = (ROOT / "upstream" / "mod.rs").read_text()
    return tuple(
        (int(match.group(1)), int(match.group(2)))
        for match in STABLE.finditer(text)
    )


def write(payload):
    return ("write", payload)


def history_cases():
    cases = {}

    def add(
        name,
        label,
        rows,
        columns,
        save_lines,
        actions,
        expected,
        history,
        stable=(),
        translated=None,
    ):
        cases[name] = (
            name,
            label,
            rows,
            columns,
            save_lines,
            tuple(actions),
            tuple(expected),
            tuple(expected if translated is None else translated),
            history,
            tuple(stable),
        )

    erase = [write(b"abc\r\ndef\r\nghi\r\n111\r\n222\r\na\x1b[3J")]
    add(
        "csi_0400", "test_ed_erase_scrollback", 3, 3, 3,
        erase, ("111", "222", "a"), 0,
    )
    erase.append(write(b"b"))
    add(
        "csi_0402", "test_ed_erase_scrollback", 3, 3, 3,
        erase, ("111", "222", "ab"), 0,
    )

    scrolling = []
    scroll_expected = (
        ("1", ""),
        ("1", "2", ""),
        ("1", "2", "3", ""),
        ("1", "2", "3", "4", ""),
        ("1", "2", "3", "4", "5", ""),
        ("2", "3", "4", "5", "6", ""),
        ("3", "4", "5", "6", "7", ""),
        ("4", "5", "6", "7", "8", ""),
    )
    scroll_lines = (1090, 1094, 1098, 1102, 1106, 1110, 1114, 1118)
    for index, (line, expected) in enumerate(
        zip(scroll_lines, scroll_expected), 1
    ):
        scrolling.append(write(f"{index}\n".encode()))
        add(
            f"mod_{line:04d}", "test_scrollup", 2, 1, 4,
            scrolling, expected, min(index - 1, 6),
            ((0, index - 1),),
            (
                tuple(str(value) for value in range(1, index + 1)) + ("",)
                if index in (6, 7)
                else (
                    tuple(str(value) for value in range(2, 9)) + ("",)
                    if index == 8
                    else None
                )
            ),
        )

    add(
        "mod_1126", "test_ri", 3, 1, 10,
        (write(b"1\n\x1bM\n"),), ("1", "", ""), 0,
    )

    margins = [write(b"1\n2\n3\n4\n")]
    add(
        "mod_1133", "test_scroll_margins", 3, 1, 10,
        margins, ("1", "2", "3", "4", ""), 2,
    )
    margins.append(write(b"\x1b[1;2rz\n"))
    add(
        "mod_1142", "test_scroll_margins", 3, 1, 10,
        margins, ("1", "2", "z", "4", ""), 2,
    )
    margins.append(write(b"a\n"))
    add(
        "mod_1145", "test_scroll_margins", 3, 1, 10,
        margins, ("1", "2", "z", "a", "", ""), 3,
    )
    margins.append(write(b"\x1b[2;1HW\n"))
    add(
        "mod_1149", "test_scroll_margins", 3, 1, 10,
        margins, ("1", "2", "z", "a", "W", "", ""), 4,
    )

    add(
        "mod_1162", "test_emoji_with_modifier", 3, 5, 0,
        (write("\U0001f44b\r\n\U0001f44b\U0001f3ff".encode()),),
        ("\U0001f44b", "\U0001f44b\U0001f3ff", ""), 0,
    )
    add(
        "mod_1178", "test_1573", 2, 5, 0,
        (write("\u1112\u1161\u11ab\r\n".encode()),),
        ("\u1112\u1161\u11ab", ""), 0,
    )

    region = [write(b"1\n2\n3\n4\n5")]
    region.append(write(b"\x1b[2;3r\x1b[3;1H\na"))
    add(
        "mod_1199", "test_region_scroll", 5, 1, 10,
        region, ("1", "3", "a", "4", "5"), 0,
        ((0, 0), (4, 4)),
    )
    region.append(write(b"\x1b[1;2r\x1b[2;1H\nb"))
    add(
        "mod_1211", "test_region_scroll", 5, 1, 10,
        region, ("1", "3", "b", "a", "4", "5"), 1,
        ((0, 1), (4, 5)),
    )
    region.append(write(b"\x1b[2;1H\x1b[3M"))
    add(
        "mod_1220", "test_region_scroll", 5, 1, 10,
        region, ("1", "3", "", "a", "4", "5"), 1,
        ((0, 1), (4, 5)),
    )
    region.append(write(b"\x1b[1;5r\x1b[5;1H\nX"))
    add(
        "mod_1230", "test_region_scroll", 5, 1, 10,
        region, ("1", "3", "", "a", "4", "5", "X"), 2,
        ((4, 6),),
    )

    alternate = [write(b"M\no\nn\nk\ne\ny")]
    alternate.append(write(b"\x1b[?1049h1\n2\n3\n4\n5"))
    alternate.append(write(b"\x1b[2;3r\x1b[3;1H\na"))
    add(
        "mod_1251", "test_alt_screen_region_scroll", 5, 1, 10,
        alternate, ("1", "3", "a", "4", "5"), 0,
        ((4, 4),),
    )
    alternate.append(write(b"\x1b[1;2r\x1b[2;1H\nb"))
    add(
        "mod_1260", "test_alt_screen_region_scroll", 5, 1, 10,
        alternate, ("3", "b", "a", "4", "5"), 0,
        ((4, 4),),
    )
    alternate.append(write(b"\x1b[1;5r\x1b[5;1H\nX"))
    add(
        "mod_1270", "test_alt_screen_region_scroll", 5, 1, 10,
        alternate, ("b", "a", "4", "5", "X"), 0,
        ((4, 4),),
    )
    alternate.append(write(b"\x1b[?1049l"))
    add(
        "mod_1277", "test_alt_screen_region_scroll", 5, 1, 10,
        alternate, ("M", "o", "n", "k", "e", "y"), 1,
        ((0, 1),),
    )

    limited = (
        write(b"1\n2\n3\n4"),
        write(b"\x1b[1;2r\x1b[2;1H"),
        write(b"A\nB\nC\nD"),
    )
    add(
        "mod_1292", "test_region_scrollback_limit", 4, 1, 2,
        limited, ("A", "B", "C", "D", "3", "4"), 3,
        ((4, 7),),
        ("1", "A", "B", "C", "D", "3", "4"),
    )

    add(
        "selection_0083", "selection_in_scrollback", 2, 2, 4,
        (write(b"1 2 3 4"),), ("1 ", "2 ", "3 ", "4 "), 2,
        translated=("1 ", "2 ", "3 ", "4"),
    )

    source = source_assertions()
    for name, upstream in source:
        case = cases.get(name)
        if case is None:
            raise ValueError(f"untranslated WezTerm history assertion {name}")
        if case[6] != upstream:
            raise ValueError(
                f"WezTerm history oracle mismatch for {name}: "
                f"source={upstream}, translated={case[6]}"
            )
        yield case


CASES = tuple(history_cases())


def case_names():
    return tuple(case[0] for case in CASES)


def case_data(name):
    for case in CASES:
        if case[0] == name:
            return case[1:]
    raise KeyError(name)
