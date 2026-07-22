#!/usr/bin/env python3

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SOURCES = tuple(ROOT / "upstream" / name for name in
                ("c0.rs", "c1.rs", "csi.rs", "mod.rs", "selection.rs"))
CALL = re.compile(r'\bterm\.print\(\s*b?("(?:\\.|[^"\\])*")\s*\)')


def decode_rust(token):
    text = token[1:-1]
    result = []
    index = 0
    escapes = {"n": "\n", "r": "\r", "t": "\t", "0": "\0",
               "\\": "\\", '"': '"'}
    while index < len(text):
        if text[index] != "\\":
            result.append(text[index])
            index += 1
            continue
        index += 1
        escape = text[index]
        if escape in escapes:
            result.append(escapes[escape])
            index += 1
        elif escape == "x":
            result.append(chr(int(text[index + 1:index + 3], 16)))
            index += 3
        elif escape == "u" and text[index + 1] == "{":
            end = text.index("}", index + 2)
            result.append(chr(int(text[index + 2:end].replace("_", ""), 16)))
            index = end + 1
        elif escape == "\n":
            index += 1
            while index < len(text) and text[index].isspace():
                index += 1
        else:
            raise ValueError(f"unsupported Rust escape \\{escape}")
    return "".join(result).encode("utf-8")


def parser_cases():
    for source in SOURCES:
        text = source.read_text()
        for match in CALL.finditer(text):
            line = text.count("\n", 0, match.start()) + 1
            yield f"{source.stem}_{line:04d}", decode_rust(match.group(1))


def case_names():
    return tuple(name for name, _ in parser_cases())


def case_payload(name):
    for candidate, payload in parser_cases():
        if candidate == name:
            return payload
    raise KeyError(name)
