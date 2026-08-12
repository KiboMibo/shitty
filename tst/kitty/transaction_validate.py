#!/usr/bin/env python3

import ast
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parent))

from transaction_cases import CASES


EXTERNAL = {
    "base64": "base64_ut.cpp",
    "utf8_simd_decode": "utf8_adapter.py",
    "find_either_of_two_bytes": "inapplicable internal SIMD helper",
    "graphics_command": "excluded graphics protocol",
    "deccara": "../test_deccara.py",
}


def upstream_methods():
    tree = ast.parse((ROOT / "upstream" / "parser.py").read_text())
    parser = next(
        node for node in tree.body
        if isinstance(node, ast.ClassDef) and node.name == "TestParser"
    )
    return {
        node.name.removeprefix("test_")
        for node in parser.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name.startswith("test_")
    }


def main():
    manifest = (ROOT / "transaction_file_names.txt").read_text().split()
    if len(manifest) != len(set(manifest)):
        raise RuntimeError("duplicate Kitty transaction")
    if set(manifest) != set(CASES):
        raise RuntimeError(
            "Kitty transaction manifest and implementation differ"
        )

    upstream = upstream_methods()
    accounted = set(manifest) | set(EXTERNAL)
    if accounted != upstream:
        missing = sorted(upstream - accounted)
        stale = sorted(accounted - upstream)
        raise RuntimeError(
            f"Kitty method accounting differs: missing={missing}, stale={stale}"
        )

    print(
        f"PASS Kitty transactions: {len(manifest)} imported, "
        f"{len(EXTERNAL)} externally accounted"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
