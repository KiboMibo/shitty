#!/usr/bin/env python3

import re
from pathlib import Path

from catalog import decode_rust


ROOT = Path(__file__).resolve().parent
SOURCES = tuple(ROOT / "upstream" / name for name in
                ("c0.rs", "c1.rs", "csi.rs", "mod.rs", "selection.rs"))
TEST = re.compile(r'^#\[test\]\s*\nfn ([A-Za-z0-9_]+)\(\) \{', re.MULTILINE)
GEOMETRY = re.compile(r'TestTerm::new\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)')
METHOD = re.compile(r'\bterm\.([A-Za-z_][A-Za-z0-9_]*)\s*\(')
STRING = re.compile(r'b?("(?:\\.|[^"\\])*")')
READ_ONLY = {
    "assert_cursor_pos", "assert_dirty_lines", "current_seqno", "screen",
    "get_semantic_zones", "get_title", "get_icon_name",
}


def call_end(text, start):
    depth = 0
    quoted = False
    escaped = False
    for index in range(start, len(text)):
        character = text[index]
        if quoted:
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == '"':
                quoted = False
            continue
        if character == '"':
            quoted = True
        elif character == "(":
            depth += 1
        elif character == ")":
            depth -= 1
            if depth == 0:
                return index + 1
    raise ValueError("unterminated Rust call")


def literal_argument(call):
    match = re.fullmatch(r'\s*b?("(?:\\.|[^"\\])*")\s*', call)
    return decode_rust(match.group(1)) if match else None


def integer_arguments(call):
    values = [part.strip() for part in call.split(",")]
    if not values or not all(re.fullmatch(r"-?\d+", value) for value in values):
        return None
    return tuple(map(int, values))


def translated_method(method, arguments):
    if method == "print":
        return literal_argument(arguments)
    values = integer_arguments(arguments)
    if method in ("cup", "hvp") and values is not None and len(values) == 2:
        column, row = values
        if column >= 0 and row >= 0:
            final = b"H" if method == "cup" else b"f"
            return f"\x1b[{row + 1};{column + 1}".encode() + final
    if method == "delete_lines" and values is not None and len(values) == 1:
        if values[0] >= 0:
            return f"\x1b[{max(values[0], 1)}M".encode()
    match = re.fullmatch(r'\s*(true|false)\s*', arguments)
    if method == "set_auto_wrap" and match:
        return b"\x1b[?7h" if match.group(1) == "true" else b"\x1b[?7l"
    match = re.fullmatch(r'\s*"([?=>]?[0-9;]+)"\s*,\s*(true|false)\s*',
                         arguments)
    if method == "set_mode" and match:
        return b"\x1b[" + match.group(1).encode() + (
            b"h" if match.group(2) == "true" else b"l")
    erase = {
        "EraseToEndOfDisplay": 0,
        "EraseToStartOfDisplay": 1,
        "EraseDisplay": 2,
        "EraseScrollback": 3,
        "EraseToEndOfLine": 0,
        "EraseToStartOfLine": 1,
        "EraseLine": 2,
    }
    match = re.search(r'::([A-Za-z]+)\s*$', arguments)
    if method in ("erase_in_display", "erase_in_line") and match:
        value = erase.get(match.group(1))
        if value is not None:
            final = "J" if method == "erase_in_display" else "K"
            return f"\x1b[{value}{final}".encode()
    return None


def expected_lines(call):
    array = re.search(r'&\[(.*)\]\s*$', call, re.DOTALL)
    if not array:
        return None
    body = array.group(1)
    matches = list(STRING.finditer(body))
    remainder = STRING.sub("", body)
    if not matches or re.sub(r"[\s,]", "", remainder):
        return None
    return tuple(decode_rust(match.group(1)).decode("utf-8") for match in matches)


def source_cases(source):
    text = source.read_text()
    tests = list(TEST.finditer(text))
    for test_index, test in enumerate(tests):
        end = tests[test_index + 1].start() if test_index + 1 < len(tests) else len(text)
        block = text[test.end():end]
        geometry = GEOMETRY.search(block)
        if not geometry:
            continue
        rows, columns, save_lines = map(int, geometry.groups())
        events = []
        for match in METHOD.finditer(block):
            events.append((match.start(), "method", match))
        for match in re.finditer(r'\bassert_visible_contents\s*\(', block):
            events.append((match.start(), "assert", match))
        payload = bytearray()
        valid = True
        for _position, kind, match in sorted(events, key=lambda event: event[0]):
            end_call = call_end(block, block.index("(", match.start()))
            opening = block.index("(", match.start())
            arguments = block[opening + 1:end_call - 1]
            if kind == "method":
                method = match.group(1)
                if method in READ_ONLY:
                    continue
                translated = translated_method(method, arguments)
                if translated is None:
                    valid = False
                else:
                    payload.extend(translated)
                continue
            expected = expected_lines(arguments)
            if not valid or expected is None:
                continue
            line = text.count("\n", 0, test.end() + match.start()) + 1
            name = f"{source.stem}_{line:04d}"
            yield name, test.group(1), rows, columns, save_lines, bytes(payload), expected


def screen_cases():
    for source in SOURCES:
        yield from source_cases(source)


def case_names():
    return tuple(name for name, *_rest in screen_cases())


def case_data(name):
    for case in screen_cases():
        if case[0] == name:
            return case[1:]
    raise KeyError(name)
