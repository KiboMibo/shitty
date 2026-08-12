#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import re
from pathlib import Path


SOURCE = Path(__file__).resolve().parent / "upstream" / "mod.rs"
SNAPSHOT = re.compile(
    r"k9::snapshot!\(\s*term\.get_semantic_zones\(\)\.unwrap\(\)\s*,"
    r'\s*"(.*?)"\s*\);',
    re.DOTALL,
)
ZONE = re.compile(
    r"SemanticZone\s*\{\s*"
    r"start_y:\s*(\d+),\s*"
    r"start_x:\s*(\d+),\s*"
    r"end_y:\s*(\d+),\s*"
    r"end_x:\s*(\d+),\s*"
    r"semantic_type:\s*(\w+),\s*"
    r"\}",
    re.DOTALL,
)


def source_zone_assertions():
    text = SOURCE.read_text()
    result = []
    for match in SNAPSHOT.finditer(text):
        line = text.count("\n", 0, match.start()) + 1
        zones = tuple(
            (
                int(zone.group(1)),
                int(zone.group(2)),
                int(zone.group(3)),
                int(zone.group(4)),
                zone.group(5),
            )
            for zone in ZONE.finditer(match.group(1))
        )
        if not zones:
            raise ValueError("empty WezTerm semantic-zone snapshot")
        result.append((f"mod_{line:04d}", zones))
    return tuple(result)


def source_attribute_assertions():
    text = SOURCE.read_text()
    begin = text.index("fn test_semantic()")
    end = text.index("\n#[test]", begin)
    block = text[begin:end]
    matches = tuple(re.finditer(r"\bassert_lines_equal\s*\(", block))
    if len(matches) != 1:
        raise ValueError(
            f"expected one semantic attribute assertion, found {len(matches)}"
        )
    line = text.count("\n", 0, begin + matches[0].start()) + 1
    return (f"mod_{line:04d}",)


def osc(marker):
    return b"\x1b]133;" + marker + b"\x1b\\"


def semantic_cases():
    output = ((0, 0, 2, 4, "Output"),)
    complete = (
        (0, 0, 2, 4, "Output"),
        (3, 0, 3, 1, "Prompt"),
        (3, 2, 3, 6, "Input"),
        (4, 0, 4, 8, "Output"),
    )
    initial = b"hello" + osc(b"L") + b"there"
    initial += b"\x1b[3;1H" + osc(b"L") + b"three"
    shell = (
        initial
        + osc(b"A")
        + b"> "
        + osc(b"B")
        + b"ls -l\r\n"
        + osc(b"C")
        + b"some file"
    )
    cases = (
        (
            "mod_0331",
            "test_semantic_1539",
            5,
            10,
            osc(b"I") + b"prompt\r\nwoot",
            (
                (0, 0, 0, 5, "Input"),
                (1, 0, 1, 3, "Output"),
            ),
            False,
        ),
        (
            "mod_0380",
            "test_semantic",
            5,
            10,
            initial,
            output,
            False,
        ),
        (
            "mod_0436",
            "test_semantic",
            5,
            10,
            shell,
            complete,
            False,
        ),
        (
            "mod_0472",
            "test_semantic_attributes",
            5,
            10,
            shell,
            complete,
            True,
        ),
    )
    source_zones = dict(source_zone_assertions())
    source_attributes = source_attribute_assertions()
    for case in cases:
        name, _label, _rows, _columns, _payload, zones, attributes = case
        if attributes:
            if (name,) != source_attributes:
                raise ValueError(
                    f"WezTerm semantic attribute oracle mismatch: "
                    f"source={source_attributes}, translated={name}"
                )
        elif source_zones.pop(name, None) != zones:
            raise ValueError(
                f"WezTerm semantic-zone oracle mismatch for {name}"
            )
        yield case
    if source_zones:
        raise ValueError(
            f"untranslated WezTerm semantic-zone assertions: {source_zones}"
        )


CASES = tuple(semantic_cases())


def case_names():
    return tuple(case[0] for case in CASES)


def case_data(name):
    for case in CASES:
        if case[0] == name:
            return case[1:]
    raise KeyError(name)
