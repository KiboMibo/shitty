#!/usr/bin/env python3

import ast
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SOURCES = (
    ROOT / "upstream" / "StateMachineTest.cpp",
    ROOT / "upstream" / "OutputEngineTest.cpp",
)
CALL = re.compile(r'ProcessString\s*\(\s*((?:L"(?:\\.|[^"\\])*"\s*)+)\)', re.DOTALL)
LITERAL = re.compile(r'L("(?:\\.|[^"\\])*")')


def encode_terminal(text):
    result = bytearray()
    for character in text:
        value = ord(character)
        if 0x80 <= value <= 0x9f:
            result.append(value)
        else:
            result.extend(character.encode("utf-8"))
    return bytes(result)


def parser_cases():
    for source in SOURCES:
        text = source.read_text()
        for match in CALL.finditer(text):
            line = text.count("\n", 0, match.start()) + 1
            value = "".join(ast.literal_eval(token)
                            for token in LITERAL.findall(match.group(1)))
            yield f"{source.stem.lower()}_{line:04d}", encode_terminal(value)


def case_names():
    return tuple(name for name, _ in parser_cases())


def case_payload(name):
    for candidate, payload in parser_cases():
        if candidate == name:
            return payload
    raise KeyError(name)
