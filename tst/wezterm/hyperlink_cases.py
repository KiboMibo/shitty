#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import re
from pathlib import Path


SOURCE = Path(__file__).resolve().parent / "upstream" / "mod.rs"


def source_assertions():
    text = SOURCE.read_text()
    begin = text.index("fn test_hyperlinks()")
    end = text.find("\n#[test]", begin)
    if end < 0:
        end = len(text)
    block = text[begin:end]
    return tuple(
        f"mod_{text.count(chr(10), 0, begin + match.start()) + 1:04d}"
        for match in re.finditer(r"\bassert_lines_equal\s*\(", block)
    )


def osc8(uri, identifier=None):
    parameters = b"" if identifier is None else b"id=" + identifier
    return b"\x1b]8;" + parameters + b";" + uri + b"\x1b\\"


FIRST = b"http://example.com"
SECOND = b"http://example.com/other"
CLOSE = osc8(b"")
CHECKPOINT_1 = osc8(FIRST) + b"hello" + CLOSE
CHECKPOINT_2 = CHECKPOINT_1 + osc8(FIRST) + b"he\x1b[my!!"
CHECKPOINT_3 = (
    CHECKPOINT_2
    + osc8(SECOND, b"w00t")
    + b"wo"
    + b"\x1b[!p"
    + b"00t"
)


CASES = (
    (
        "mod_1308",
        "test_hyperlinks/initial",
        CHECKPOINT_1,
        ("hello", "", ""),
        ("AAAAA", "", ""),
    ),
    (
        "mod_1326",
        "test_hyperlinks/SGR-reset",
        CHECKPOINT_2,
        ("hello", "hey!!", ""),
        ("AAAAA", "AAAAA", ""),
    ),
    (
        "mod_1369",
        "test_hyperlinks/DECSTR",
        CHECKPOINT_3,
        ("hello", "hey!!", "wo00t"),
        ("AAAAA", "AAAAA", "BB..."),
    ),
)


def checked_cases():
    source = source_assertions()
    implemented = tuple(case[0] for case in CASES)
    if source != implemented:
        raise ValueError(
            "WezTerm hyperlink oracle mismatch: "
            f"source={source}, translated={implemented}"
        )
    return CASES


def case_names():
    return tuple(case[0] for case in checked_cases())


def case_data(name):
    for case in checked_cases():
        if case[0] == name:
            return case[1:]
    raise KeyError(name)
