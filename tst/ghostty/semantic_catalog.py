#!/usr/bin/env python3

import ast
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "upstream" / "stream_terminal_tests.zig"
TEST = re.compile(r'^test "([^"]+)" \{', re.MULTILINE)
WRITE = re.compile(r'\bs\.nextSlice\(\s*("(?:\\.|[^"\\])*")\s*\)')


def parser_cases():
    text = SOURCE.read_text()
    tests = list(TEST.finditer(text))
    for index, match in enumerate(tests):
        end = tests[index + 1].start() if index + 1 < len(tests) else len(text)
        chunks = tuple(
            ast.literal_eval(write.group(1)).encode("utf-8")
            for write in WRITE.finditer(text, match.end(), end)
        )
        if not chunks:
            continue
        line = text.count("\n", 0, match.start()) + 907
        yield f"stream_terminal_{line:04d}", match.group(1), chunks


def case_names():
    return tuple(name for name, _, _ in parser_cases())


def case_payload(name):
    for candidate, label, chunks in parser_cases():
        if candidate == name:
            return label, chunks
    raise KeyError(name)
