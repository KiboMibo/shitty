#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent / "upstream"
CSI = ROOT / "csi.rs"
MOD = ROOT / "mod.rs"


def function_block(text, name):
    begin = text.index(f"fn {name}()")
    end = text.find("\n#[test]", begin)
    if end < 0:
        end = len(text)
    return begin, text[begin:end]


def source_assertions():
    csi = CSI.read_text()
    mod = MOD.read_text()
    result = []

    begin, block = function_block(csi, "test_789")
    snapshots = tuple(re.finditer(r"\bk9::snapshot!\s*\(", block))
    if len(snapshots) != 1:
        raise ValueError(
            f"expected one test_789 metadata snapshot, found {len(snapshots)}"
        )
    line = csi.count("\n", 0, begin + snapshots[0].start()) + 1
    result.append(f"csi_{line:04d}")

    begin, block = function_block(csi, "test_ed")
    attributes = tuple(re.finditer(r"\bassert_lines_equal\s*\(", block))
    if len(attributes) != 1:
        raise ValueError(
            f"expected one test_ed attribute assertion, found {len(attributes)}"
        )
    line = csi.count("\n", 0, begin + attributes[0].start()) + 1
    result.append(f"csi_{line:04d}")

    begin, block = function_block(mod, "test_dec_double_width")
    modes = tuple(
        re.finditer(
            r"\bassert!\s*\(\s*lines\[\d+\]\.is_"
            r"(?:double_height_top|double_height_bottom|double_width|single_width)"
            r"\(\)\s*\)",
            block,
        )
    )
    if len(modes) != 4:
        raise ValueError(
            f"expected four DEC line-mode assertions, found {len(modes)}"
        )
    result.extend(
        f"mod_{mod.count(chr(10), 0, begin + match.start()) + 1:04d}"
        for match in modes
    )

    begin, block = function_block(mod, "test_1573")
    unicode_assertions = tuple(re.finditer(r"\bassert_eq!\s*\(", block))
    if len(unicode_assertions) != 2:
        raise ValueError(
            "expected NFC and grapheme assertions in test_1573, "
            f"found {len(unicode_assertions)}"
        )
    result.extend(
        f"mod_{mod.count(chr(10), 0, begin + match.start()) + 1:04d}"
        for match in unicode_assertions
    )
    return tuple(result)


DCH = b"\x1b[40m\x1b[Kfoo\x1b[2P"
ED = b"abc\r\ndef\r\nghi\x1b[3;2H\x1b[J\x1b[44m\x1b[2J"
LINE_MODES = b"\x1b#3line1\r\nline2\x1b#4\r\nli\x1b#6ne3\r\n\x1b#5line4"
HANGUL = "\u1112\u1161\u11ab".encode()

CASES = (
    ("csi_0012", "test_789/DCH-BCE", 1, 8, DCH, "cells", 0),
    ("csi_0387", "test_ed/BCE", 3, 3, ED, "cells", 4),
    (
        "mod_0757",
        "test_dec_double_width/top",
        4,
        50,
        LINE_MODES,
        "line",
        1,
    ),
    (
        "mod_0758",
        "test_dec_double_width/bottom",
        4,
        50,
        LINE_MODES,
        "line",
        2,
    ),
    (
        "mod_0759",
        "test_dec_double_width/double",
        4,
        50,
        LINE_MODES,
        "line",
        3,
    ),
    (
        "mod_0760",
        "test_dec_double_width/single",
        4,
        50,
        LINE_MODES,
        "line",
        0,
    ),
    ("mod_1182", "test_1573/NFC", 2, 5, HANGUL, "nfc", "\ud55c"),
    (
        "mod_1186",
        "test_1573/grapheme",
        2,
        5,
        HANGUL,
        "grapheme",
        (0x1112, 0x1161, 0x11AB),
    ),
)


def checked_cases():
    source = source_assertions()
    implemented = tuple(case[0] for case in CASES)
    if source != implemented:
        raise ValueError(
            "WezTerm metadata oracle mismatch: "
            f"source={source}, translated={implemented}"
        )
    return CASES


def case_names():
    return tuple(case[0] for case in checked_cases())


def case_data(name):
    for index, case in enumerate(checked_cases()):
        if case[0] == name:
            return index, case[1:]
    raise KeyError(name)
