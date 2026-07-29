#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

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


def assertion_names():
    names = []
    for source in SOURCES:
        text = source.read_text()
        tests = list(TEST.finditer(text))
        for test_index, test in enumerate(tests):
            end = tests[test_index + 1].start() if test_index + 1 < len(tests) else len(text)
            block = text[test.end():end]
            for match in re.finditer(r'\bassert_visible_contents\s*\(', block):
                line = text.count("\n", 0, test.end() + match.start()) + 1
                names.append((f"{source.stem}_{line:04d}", test.group(1)))
    return tuple(names)


def write(data):
    if isinstance(data, str):
        data = data.encode()
    return ("write", data)


def resize(columns, rows):
    return ("resize", columns, rows)


def manual_cases():
    cases = {}

    def add(name, label, rows, columns, actions, expected, save_lines=0):
        cases[name] = (
            name,
            label,
            rows,
            columns,
            save_lines,
            tuple(actions),
            tuple(expected),
        )

    delete_lines = b"a\r\nb\r\nc\x1b[2;1H\x1b[M\x1b[1;1H\x1b[2M1\r\n2\r\n3\x1b[2;1H\x1b[-2M"
    add("csi_0341", "test_dl", 3, 4, [write(delete_lines)], ["1", "2", "3"])

    osc = lambda marker: f"\x1b]133;{marker}\x1b\\".encode()
    add("mod_0329", "test_semantic_1539", 5, 10, [write(osc("I") + b"prompt\r\nwoot")], ["prompt", "woot", "", "", ""])
    semantic = b"hello" + osc("L") + b"there"
    add("mod_0365", "test_semantic", 5, 10, [write(semantic)], ["hello", "there", "", "", ""])
    add("mod_0373", "test_semantic", 5, 10, [write(semantic + b"\x1b[3;1H" + osc("L") + b"three")], ["hello", "there", "three", "", ""])
    add("mod_0491", "issue_1161", 1, 5, [write("x\u3000x")], ["x\u3000x"])

    basic = b"\x1b[2;2H\x1b[?7lhello, world!\x1b[?7h\x1b[1J\x1b[2;2Hhello, world!\x1b[1J"
    add("mod_0519", "basic_output", 5, 10, [write(basic)], ["", "          ", "     ", "", ""])

    initial_margin_rows = b"111\r\n222\r\n333\r\n44444\r\n555"
    add("mod_0587", "scroll_up_within_left_and_right_margins", 5, 5, [write(initial_margin_rows)], ["111", "222", "333", "44444", "555"])
    scroll_up = initial_margin_rows + b"\x1b[?69h\x1b[2;5s\x1b[3;5r\x1b[5;2H\n"
    add("mod_0599", "scroll_up_within_left_and_right_margins", 5, 5, [write(scroll_up)], ["111", "222", "34444", "455", "5"])
    add("mod_0633", "scroll_down_within_left_and_right_margins", 5, 5, [write(initial_margin_rows)], ["111", "222", "333", "44444", "555"])
    scroll_down = initial_margin_rows + b"\x1b[?69h\x1b[2;5s\x1b[3;6r\x1b[3;2H\x1b[L"
    add("mod_0649", "scroll_down_within_left_and_right_margins", 5, 5, [write(scroll_down)], ["111", "222", "3", "433", "54444"])

    delete_base = b"111\r\n222\r\n333\r\n444\r\n555"
    add("mod_0673", "test_delete_lines", 5, 3, [write(delete_base)], ["111", "222", "333", "444", "555"])
    after_delete = delete_base + b"\x1b[2;1H\x1b[2M\x1b[4;1Haaa\r\nbbb\x1b[2;1H"
    add("mod_0693", "test_delete_lines", 5, 3, [write(after_delete)], ["111", "444", "555", "aaa", "bbb"])
    after_region_delete = after_delete + b"\x1b[2;4r\x1b[2;1H\x1b[2M"
    add("mod_0706", "test_delete_lines", 5, 3, [write(after_region_delete)], ["111", "aaa", "", "", "bbb"])
    add("mod_0716", "test_delete_lines", 5, 3, [write(after_region_delete + b"\x1b[1;5r\x1b[M")], ["aaa", "", "", "bbb", ""])

    add("mod_0726", "test_dec_special_graphics", 2, 50, [write("\x1b(0ABCabcdefghijklmnopqrstuvwxyzDEF\r\n\x1b(Bhello")], ["ABC▒␉␌␍␊°±␤␋┘┐┌└┼⎺⎻─⎼⎽├┤┴┬│≤≥DEF", "hello"])
    add("mod_0735", "test_dec_special_graphics", 2, 50, [write("\x1b)0\x0eSO-ABCabcdefghijklmnopqrstuvwxyzDEF\r\n\x0fSI-hello")], ["SO-ABC▒␉␌␍␊°±␤␋┘┐┌└┼⎺⎻─⎼⎽├┤┴┬│≤≥DEF", "SI-hello"])
    add("mod_0749", "test_dec_double_width", 4, 50, [write("\x1b#3line1\r\nline2\x1b#4\r\nli\x1b#6ne3\r\n\x1b#5line4")], ["line1", "line2", "line3", "line4"])

    text = b"some long long text"
    add("mod_0773", "test_resize_2162_by_2_then_up_1", 4, 20, [write(text)], ["some long long text", "", "", ""])
    add("mod_0787", "test_resize_2162_by_2_then_up_1", 4, 20, [write(text), resize(18, 4)], ["some long long tex", "t", "", ""])
    add("mod_0802", "test_resize_2162_by_2_then_up_1", 4, 20, [write(text), resize(18, 4), resize(20, 3)], ["some long long text", "", ""])
    add("mod_0812", "test_resize_2162_by_2_then_up_1", 4, 20, [write(text), resize(18, 4), resize(20, 3), resize(20, 4)], ["some long long text", "", "", ""])
    add("mod_0831", "test_resize_2162_by_2", 4, 20, [write(text)], ["some long long text", "", "", ""])
    add("mod_0845", "test_resize_2162_by_2", 4, 20, [write(text), resize(18, 4)], ["some long long tex", "t", "", ""])
    add("mod_0860", "test_resize_2162_by_2", 4, 20, [write(text), resize(18, 4), resize(20, 4)], ["some long long text", "", "", ""])
    add("mod_0880", "test_resize_2162", 4, 20, [write(text)], ["some long long text", "", "", ""])
    add("mod_0894", "test_resize_2162", 4, 20, [write(text), resize(19, 4)], ["some long long text", "", "", ""])
    add("mod_0909", "test_resize_2162", 4, 20, [write(text), resize(19, 4), resize(20, 4)], ["some long long text", "", "", ""])

    resize_text = b"111\r\n2222aa\r\n333\r\n"
    resize_steps = (
        ("mod_0926", None, ["111", "2222", "aa", "333", "", "", "", ""]),
        ("mod_0939", 5, ["111", "2222a", "a", "333", "", "", "", ""]),
        ("mod_0952", 6, ["111", "2222aa", "333", "", "", "", "", ""]),
        ("mod_0965", 7, ["111", "2222aa", "333", "", "", "", "", ""]),
        ("mod_0976", 8, ["111", "2222aa", "333", "", "", "", "", ""]),
        ("mod_0989", 7, ["111", "2222aa", "333", "", "", "", "", ""]),
        ("mod_1000", 6, ["111", "2222aa", "333", "", "", "", "", ""]),
        ("mod_1011", 5, ["111", "2222a", "a", "333", "", "", "", ""]),
        ("mod_1022", 4, ["111", "2222", "aa", "333", "", "", "", ""]),
    )
    actions = [write(resize_text)]
    for name, columns, expected in resize_steps:
        if columns is not None:
            actions.append(resize(columns, 8))
        add(name, "test_resize_wrap", 8, 4, actions, expected)

    resize_regressions = (
        ("mod_1035", "test_resize_wrap_issue_971", b"====\r\nSS\r\n", ["====", "SS", "", ""]),
        ("mod_1049", "test_resize_wrap_sgc_issue_978", "\x1b(0qqqq\x1b(B\r\nSS\r\n", ["────", "SS", "", ""]),
        ("mod_1063", "test_resize_wrap_dectcm_issue_978", b"\x1b[?25l====\x1b[?25h\r\nSS\r\n", ["====", "SS", "", ""]),
        ("mod_1077", "test_resize_wrap_escape_code_issue_978", b"====\x1b[0m\r\nSS\r\n", ["====", "SS", "", ""]),
    )
    for initial_name, label, payload, expected in resize_regressions:
        line = int(initial_name.split("_")[1])
        resized_name = f"mod_{line + 6:04d}"
        add(initial_name, label, 4, 4, [write(payload)], expected)
        add(resized_name, label, 4, 4, [write(payload), resize(6, 4)], expected)

    add("selection_0017", "drag_selection", 3, 12, [write("hello world\r\n💀skull\r\n")], ["hello world ", "💀skull     ", "            "])
    return cases


def screen_cases():
    automatic = {}
    for source in SOURCES:
        automatic.update((case[0], case) for case in source_cases(source))
    manual = manual_cases()
    for name, _label in assertion_names():
        if name in manual:
            yield manual[name]
        elif name in automatic:
            case = automatic[name]
            yield (*case[:5], (write(case[5]),), case[6])
        else:
            raise RuntimeError(f"untranslated WezTerm screen checkpoint: {name}")


def case_names():
    return tuple(name for name, *_rest in screen_cases())


def case_data(name):
    for case in screen_cases():
        if case[0] == name:
            return case[1:]
    raise KeyError(name)
