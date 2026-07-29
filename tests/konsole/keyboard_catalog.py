#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import ast
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "upstream" / "KeyboardTranslatorTest.cpp"
QBYTE_ARRAY = re.compile(r'QByteArray\(("(?:\\.|[^"\\])*")\)')
ROW = re.compile(
    r'QTest::newRow\(("(?:\\.|[^"\\])*")\)\s*<<\s*(.*?)\s*'
    r'<<\s*(.*?)\s*<<\s*(true|false)\s*'
    r'<<\s*Qt::KeyboardModifiers\((.*?)\)\s*;',
    re.DOTALL,
)
ENTRY = re.compile(r"entry\[(\d+)\]")
MODIFIERS = {
    "Qt::NoModifier": 0,
    "Qt::ShiftModifier": 1,
    "Qt::ControlModifier": 2,
    "Qt::AltModifier": 4,
}


def cpp_string(literal):
    value = ast.literal_eval(literal)
    if not isinstance(value, str):
        raise ValueError(f"not a C++ string literal: {literal}")
    return value.encode("latin1")


def operand(expression, entries):
    expression = expression.strip()
    match = QBYTE_ARRAY.fullmatch(expression)
    if match:
        return cpp_string(match.group(1))
    match = ENTRY.fullmatch(expression)
    if match:
        return entries[int(match.group(1))]
    raise ValueError(f"unsupported QByteArray expression: {expression}")


def modifier_mask(expression):
    result = 0
    for item in expression.split("|"):
        item = item.strip()
        try:
            result |= MODIFIERS[item]
        except KeyError as error:
            raise ValueError(f"unsupported Qt modifier: {item}") from error
    return result


def keyboard_cases():
    text = SOURCE.read_text()
    entries = ()
    position = 0
    while True:
        row = ROW.search(text, position)
        if row is None:
            return
        prefix = text[position:row.start()]
        definitions = list(re.finditer(
            r"^\s*entry\s*<<\s*(.*);\s*$",
            prefix,
            re.MULTILINE,
        ))
        if definitions:
            entries = tuple(
                cpp_string(match.group(1))
                for match in QBYTE_ARRAY.finditer(definitions[-1].group(1))
            )
        line = text.count("\n", 0, row.start()) + 1
        yield (
            f"row_{line:04d}",
            cpp_string(row.group(1)).decode("latin1"),
            operand(row.group(2), entries),
            operand(row.group(3), entries),
            row.group(4) == "true",
            modifier_mask(row.group(5)),
        )
        position = row.end()


def case_names():
    return tuple(case[0] for case in keyboard_cases())


def case_vector(name):
    for case in keyboard_cases():
        if case[0] == name:
            return case[1:]
    raise KeyError(name)
