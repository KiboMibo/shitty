#!/usr/bin/env python3

import ast
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "upstream" / "Vt102EmulationTest.cpp"
ROW = re.compile(
    r'QTest::newRow\("([^"]+)"\)\s*<<\s*C\{([^}]*)\}',
    re.DOTALL,
)
CHARACTER = re.compile(r"'(?:\\.|[^'\\])'")


def character_value(token):
    value = ast.literal_eval(token)
    if not isinstance(value, str) or len(value) != 1:
        raise ValueError(f"not a C++ character literal: {token}")
    return ord(value)


def expression_value(expression):
    expression = expression.strip()
    if expression == "ESC":
        return 0x1b
    if expression.isdecimal():
        return int(expression)
    operands = [part.strip() for part in expression.split("-")]
    if len(operands) == 1 and CHARACTER.fullmatch(operands[0]):
        return character_value(operands[0])
    if len(operands) == 2 and all(CHARACTER.fullmatch(part) for part in operands):
        return character_value(operands[0]) - character_value(operands[1])
    raise ValueError(f"unsupported C++ input expression: {expression}")


def parser_cases():
    text = SOURCE.read_text()
    vt52_start = text.index("void Vt102EmulationTest::testTokenizingVT52_data()")
    for match in ROW.finditer(text):
        line = text.count("\n", 0, match.start()) + 1
        mode = "vt52" if match.start() >= vt52_start else "ansi"
        values = [expression_value(part) for part in match.group(2).split(",")]
        payload = bytes(values)
        if mode == "vt52":
            payload = b"\x1b[?2l" + payload
        yield f"{mode}_{line:04d}", match.group(1), payload


def case_names():
    return tuple(name for name, _, _ in parser_cases())


def case_payload(name):
    for candidate, label, payload in parser_cases():
        if candidate == name:
            return label, payload
    raise KeyError(name)
