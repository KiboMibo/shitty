import ast
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "upstream" / "parser.py"


def parser_cases():
    tree = ast.parse(SOURCE.read_text())
    for node in sorted(ast.walk(tree), key=lambda item: getattr(item, "lineno", 0)):
        if not (
            isinstance(node, ast.Call)
            and isinstance(node.func, ast.Name)
            and node.func.id == "pb"
            and node.args
        ):
            continue
        try:
            value = ast.literal_eval(node.args[0])
        except (ValueError, TypeError):
            continue
        if isinstance(value, str):
            value = value.encode("utf-8")
        if isinstance(value, bytes):
            yield f"parser_{node.lineno:04d}", value


def case_names():
    return tuple(name for name, _ in parser_cases())


def case_payload(name):
    for candidate, payload in parser_cases():
        if candidate == name:
            return payload
    raise KeyError(name)
