# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import ast
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "upstream" / "parser.py"


def utf8_cases():
    tree = ast.parse(SOURCE.read_text())
    method = next(
        node for node in ast.walk(tree)
        if isinstance(node, ast.FunctionDef)
        and node.name == "test_utf8_simd_decode"
    )
    for call in sorted(
        ast.walk(method),
        key=lambda node: getattr(node, "lineno", 0),
    ):
        if not (
            isinstance(call, ast.Call)
            and isinstance(call.func, ast.Name)
            and call.func.id == "pb"
            and len(call.args) >= 2
        ):
            continue
        if call.lineno == 270:
            yield call.lineno, chr(0x10FFFF), chr(0x10FFFF)
            continue
        try:
            source = ast.literal_eval(call.args[0])
            expected = ast.literal_eval(call.args[1])
        except (ValueError, TypeError):
            continue
        yield call.lineno, source, expected
