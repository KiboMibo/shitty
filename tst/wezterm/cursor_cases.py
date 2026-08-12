#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import re

from screen_catalog import (
    GEOMETRY,
    METHOD,
    READ_ONLY,
    SOURCES,
    TEST,
    call_end,
    integer_arguments,
    literal_argument,
    write,
    resize,
)


CURSOR = "assert_cursor_pos"
EXPECTED_ADAPTATIONS = {
    # WezTerm exposes its one-past-the-grid internal cursor coordinate here.
    # DEC/xterm behavior clamps an out-of-bounds CUP/HVP to the last cell.
    "csi_0308": (3, 2),
    "csi_0323": (3, 2),
    "csi_0364": (3, 1),
    "mod_0901": (18, 0),
}
PENDING_WRAP = {"mod_0901"}


def cursor_expected(arguments):
    match = re.match(r"\s*(-?\d+)\s*,\s*(-?\d+)\s*,", arguments)
    if match is None:
        return None
    return tuple(map(int, match.groups()))


def translate(method, arguments):
    if method == "print":
        payload = literal_argument(arguments)
        return write(payload) if payload is not None else None
    values = integer_arguments(arguments)
    if method in ("cup", "hvp") and values is not None and len(values) == 2:
        column, row = values
        final = "H" if method == "cup" else "f"
        return write(f"\x1b[{row + 1};{column + 1}{final}".encode())
    if method == "delete_lines" and values is not None and len(values) == 1:
        return write(f"\x1b[{values[0]}M".encode())
    return None


def automatic_cases():
    result = {}
    for source in SOURCES:
        text = source.read_text()
        tests = list(TEST.finditer(text))
        for test_index, test in enumerate(tests):
            end = (
                tests[test_index + 1].start()
                if test_index + 1 < len(tests)
                else len(text)
            )
            block = text[test.end():end]
            geometry = GEOMETRY.search(block)
            if geometry is None:
                continue
            rows, columns, save_lines = map(int, geometry.groups())
            actions = []
            valid = True
            for match in METHOD.finditer(block):
                opening = block.index("(", match.start())
                end_call = call_end(block, opening)
                arguments = block[opening + 1:end_call - 1]
                method = match.group(1)
                if method == CURSOR:
                    expected = cursor_expected(arguments)
                    if valid and expected is not None:
                        line = text.count(
                            "\n", 0, test.end() + match.start()
                        ) + 1
                        name = f"{source.stem}_{line:04d}"
                        result[name] = (
                            name,
                            test.group(1),
                            rows,
                            columns,
                            save_lines,
                            tuple(actions),
                            expected,
                        )
                    continue
                if method in READ_ONLY:
                    continue
                action = translate(method, arguments)
                if action is None:
                    valid = False
                else:
                    actions.append(action)
    return result


def manual_cases():
    result = {}

    def add(name, label, rows, columns, actions, expected, save_lines=0):
        result[name] = (
            name,
            label,
            rows,
            columns,
            save_lines,
            tuple(actions),
            expected,
        )

    hts = [write(b"boo\x1bH\r\n\t\t"), resize(80, 4), write(b"\x1b[2;1H")]
    for name, payload, expected in (
        ("c1_0051", b"\t", (3, 1)),
        ("c1_0053", b"\t", (8, 1)),
        ("c1_0055", b"\t", (16, 1)),
        ("c1_0057", b"\t", (24, 1)),
        ("c1_0059", b"\t", (32, 1)),
    ):
        hts.append(write(payload))
        add(name, "test_hts", 3, 25, hts, expected)

    text = write(b"some long long text")

    actions = [text]
    add("mod_0779", "test_resize_2162_by_2_then_up_1", 4, 20, actions, (19, 0))
    actions.append(resize(18, 4))
    add("mod_0794", "test_resize_2162_by_2_then_up_1", 4, 20, actions, (1, 1))
    actions.append(resize(20, 3))
    add("mod_0804", "test_resize_2162_by_2_then_up_1", 4, 20, actions, (19, 0))
    actions.append(resize(20, 4))
    add("mod_0819", "test_resize_2162_by_2_then_up_1", 4, 20, actions, (19, 0))

    actions = [text]
    add("mod_0837", "test_resize_2162_by_2", 4, 20, actions, (19, 0))
    actions.append(resize(18, 4))
    add("mod_0852", "test_resize_2162_by_2", 4, 20, actions, (1, 1))
    actions.append(resize(20, 4))
    add("mod_0867", "test_resize_2162_by_2", 4, 20, actions, (19, 0))

    actions = [text]
    add("mod_0886", "test_resize_2162", 4, 20, actions, (19, 0))
    actions.append(resize(19, 4))
    add("mod_0901", "test_resize_2162", 4, 20, actions, (19, 0))
    actions.append(resize(20, 4))
    add("mod_0916", "test_resize_2162", 4, 20, actions, (19, 0))

    return result


def assertion_names():
    result = []
    pattern = re.compile(r"\bterm\.assert_cursor_pos\s*\(")
    for source in SOURCES:
        text = source.read_text()
        for match in pattern.finditer(text):
            line = text.count("\n", 0, match.start()) + 1
            result.append(f"{source.stem}_{line:04d}")
    return tuple(result)


def cursor_cases():
    automatic = automatic_cases()
    manual = manual_cases()
    for name in assertion_names():
        if name in manual:
            case = manual[name]
        elif name in automatic:
            case = automatic[name]
        else:
            raise ValueError(f"untranslated WezTerm cursor assertion {name}")
        if name in EXPECTED_ADAPTATIONS:
            case = (*case[:-1], EXPECTED_ADAPTATIONS[name])
        yield case


CASES = tuple(cursor_cases())


def case_names():
    return tuple(case[0] for case in CASES)


def case_data(name):
    for case in CASES:
        if case[0] == name:
            pending = True if name in PENDING_WRAP else None
            return (*case[1:], pending)
    raise KeyError(name)
