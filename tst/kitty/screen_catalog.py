#!/usr/bin/env python3

import ast
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "upstream" / "parser.py"


def literal_bytes(node):
    try:
        value = ast.literal_eval(node)
    except (ValueError, TypeError):
        return None
    if isinstance(value, str):
        return value.encode("utf-8")
    return value if isinstance(value, bytes) else None


def line_assertion(node):
    if not (isinstance(node, ast.Expr) and isinstance(node.value, ast.Call)):
        return None
    call = node.value
    if not (isinstance(call.func, ast.Attribute) and call.func.attr == "ae"
            and len(call.args) >= 2):
        return None
    rendered = call.args[0]
    if not (isinstance(rendered, ast.Call) and isinstance(rendered.func, ast.Name)
            and rendered.func.id == "str" and len(rendered.args) == 1):
        return None
    line = rendered.args[0]
    if not (isinstance(line, ast.Call) and isinstance(line.func, ast.Attribute)
            and line.func.attr == "line" and isinstance(line.func.value, ast.Name)
            and line.func.value.id == "s" and len(line.args) == 1):
        return None
    try:
        row = ast.literal_eval(line.args[0])
        expected = ast.literal_eval(call.args[1])
    except (ValueError, TypeError):
        return None
    if not isinstance(row, int) or not isinstance(expected, str):
        return None
    return row, expected


def screen_cases():
    tree = ast.parse(SOURCE.read_text())
    parser_class = next(node for node in tree.body
                        if isinstance(node, ast.ClassDef) and node.name == "TestParser")
    for function in (node for node in parser_class.body
                     if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))):
        payload = bytearray()
        valid = False
        columns = rows = 5
        for node in function.body:
            if isinstance(node, ast.Assign):
                value = node.value
                if (isinstance(value, ast.Call)
                        and isinstance(value.func, ast.Attribute)
                        and value.func.attr == "create_screen"):
                    if any(isinstance(target, ast.Name) and target.id == "s"
                           for target in node.targets):
                        try:
                            columns = ast.literal_eval(value.args[0]) if value.args else 5
                            rows = ast.literal_eval(value.args[1]) if len(value.args) > 1 else 5
                        except (ValueError, TypeError):
                            columns = rows = 0
                        payload.clear()
                        valid = (isinstance(columns, int) and columns > 0
                                 and isinstance(rows, int) and rows > 0)
                continue
            if isinstance(node, ast.Expr) and isinstance(node.value, ast.Call):
                call = node.value
                if isinstance(call.func, ast.Name) and call.func.id == "pb":
                    value = literal_bytes(call.args[0]) if call.args else None
                    if value is None:
                        valid = False
                    elif valid:
                        payload.extend(value)
                    continue
                if (isinstance(call.func, ast.Attribute)
                        and isinstance(call.func.value, ast.Name)
                        and call.func.value.id == "s"):
                    if call.func.attr == "reset" and valid:
                        payload.extend(b"\x1bc")
                    else:
                        valid = False
                    continue
            assertion = line_assertion(node)
            if valid and assertion is not None:
                row, expected = assertion
                if any(ord(character) < 0x20 for character in expected):
                    continue
                yield (f"parser_{node.lineno:04d}", function.name, row,
                       columns, rows, bytes(payload), expected)


def case_names():
    return tuple(name for name, *_rest in screen_cases())


def case_data(name):
    for case in screen_cases():
        if case[0] == name:
            return case[1:]
    raise KeyError(name)
