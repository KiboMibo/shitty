#!/usr/bin/env python3

import ast
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "upstream" / "Vt102EmulationTest.cpp"
ROW = re.compile(
    r'QTest::newRow\("([^"]+)"\)\s*<<\s*C\{([^}]*)\}\s*'
    r'<<\s*I\{(.*?)\}\s*;',
    re.DOTALL,
)
CHARACTER = re.compile(r"'(?:\\.|[^'\\])'")
TOKEN = re.compile(
    r"ProcessToken\{token_([a-z0-9_]+)\((.*?)\),\s*"
    r"([^,{}]+),\s*([^{}]+)\}"
)


def character_value(token):
    value = ast.literal_eval(token)
    if not isinstance(value, str) or len(value) != 1:
        raise ValueError(f"not a C++ character literal: {token}")
    return ord(value)


def expression_value(expression):
    expression = expression.strip()
    if expression == "ESC":
        return 0x1b
    operands = [part.strip() for part in expression.split("-")]
    if len(operands) == 1 and CHARACTER.fullmatch(operands[0]):
        return character_value(operands[0])
    if len(operands) == 2 and all(CHARACTER.fullmatch(part) for part in operands):
        return character_value(operands[0]) - character_value(operands[1])
    try:
        return int(expression, 0)
    except ValueError:
        pass
    raise ValueError(f"unsupported C++ input expression: {expression}")


def expected_tokens(expression):
    result = []
    for match in TOKEN.finditer(expression):
        arguments = tuple(
            expression_value(item)
            for item in match.group(2).split(",")
            if item.strip()
        )
        result.append((
            match.group(1),
            arguments,
            expression_value(match.group(3)),
            expression_value(match.group(4)),
        ))
    if len(result) != expression.count("ProcessToken"):
        raise ValueError(f"unsupported ProcessToken expression: {expression}")
    return tuple(result)


def parser_cases():
    text = SOURCE.read_text()
    vt52_start = text.index("void Vt102EmulationTest::testTokenizingVT52_data()")
    for match in ROW.finditer(text):
        line = text.count("\n", 0, match.start()) + 1
        mode = "vt52" if match.start() >= vt52_start else "ansi"
        values = [expression_value(part) for part in match.group(2).split(",")]
        tokens = expected_tokens(match.group(3))
        yield (
            f"{mode}_{line:04d}",
            match.group(1),
            mode,
            bytes(values),
            tokens,
        )


def case_names():
    return tuple(name for name, _label, _mode, _payload, _tokens in parser_cases())


def case_payload(name):
    for candidate, label, mode, payload, _tokens in parser_cases():
        if candidate == name:
            if mode == "vt52":
                payload = b"\x1b[?2l" + payload
            return label, payload
    raise KeyError(name)


def case_spec(name):
    for candidate, label, mode, payload, tokens in parser_cases():
        if candidate == name:
            return label, mode, payload, tokens
    raise KeyError(name)
