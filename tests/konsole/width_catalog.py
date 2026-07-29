#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import ast
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "upstream" / "CharacterWidthTest.cpp"
ROW = re.compile(
    r'QTest::newRow\("([^"]+)"\)\s*<<\s*uint\(([^)]+)\)\s*'
    r'<<\s*(-?\d+)\s*;'
)


def expression_value(expression):
    expression = expression.strip()
    if expression.startswith("'"):
        value = ast.literal_eval(expression)
        if not isinstance(value, str) or len(value) != 1:
            raise ValueError(f"invalid character literal: {expression}")
        return ord(value)
    return int(expression, 0)


def width_cases():
    for match in ROW.finditer(SOURCE.read_text()):
        label = match.group(1)
        codepoint = expression_value(match.group(2))
        upstream_width = int(match.group(3))
        width = 0 if upstream_width < 0 else upstream_width
        yield f"u{codepoint:06x}", label, codepoint, width


def case_names():
    return tuple(name for name, _label, _codepoint, _width in width_cases())


def case_vector(name):
    for candidate, label, codepoint, width in width_cases():
        if candidate == name:
            return label, codepoint, width
    raise KeyError(name)
