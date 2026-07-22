#!/usr/bin/env python3

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "upstream" / "unicode-width-test.cc"
RANGE = re.compile(
    r'for \(c = (0x[0-9A-Fa-f]+); c < (0x[0-9A-Fa-f]+); c\+\+\) \{\s*'
    r'g_assert_cmpint\(_vte_unichar_width\(c, 1\), ==, ([012])\);',
    re.DOTALL,
)
POINT = re.compile(
    r'g_assert_cmpint\(_vte_unichar_width\((0x[0-9A-Fa-f]+), 1\), '
    r'==, ([012])\);'
)


def width_cases():
    text = SOURCE.read_text()
    for match in RANGE.finditer(text):
        start = int(match.group(1), 0)
        end = int(match.group(2), 0)
        width = int(match.group(3))
        shard_start = start
        while shard_start < end:
            shard_end = min((shard_start | 0xff) + 1, end)
            name = f"range_{shard_start:06x}_{shard_end - 1:06x}"
            yield name, tuple((codepoint, width)
                              for codepoint in range(shard_start, shard_end))
            shard_start = shard_end
    points = tuple((int(match.group(1), 0), int(match.group(2)))
                   for match in POINT.finditer(text))
    yield "points", points


def case_names():
    return tuple(name for name, _ in width_cases())


def case_vectors(name):
    for candidate, vectors in width_cases():
        if candidate == name:
            return vectors
    raise KeyError(name)
