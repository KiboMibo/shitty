#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

from pathlib import Path

from keyboard_catalog import case_names, keyboard_cases


ROOT = Path(__file__).resolve().parent
manifest = tuple((ROOT / "keyboard_file_names.txt").read_text().split())
cases = tuple(keyboard_cases())
expected = case_names()
if len(cases) != 24:
    raise SystemExit(
        f"expected 24 Konsole KeyboardTranslator rows, found {len(cases)}"
    )
if len(set(expected)) != len(expected):
    raise SystemExit("duplicate Konsole KeyboardTranslator rows")
if manifest != expected:
    raise SystemExit(
        "Konsole KeyboardTranslator catalog mismatch: "
        f"missing={sorted(set(expected) - set(manifest))}, "
        f"extra={sorted(set(manifest) - set(expected))}"
    )
for name, label, text, result, wildcards, _modifiers in cases:
    if not wildcards and text != result:
        raise SystemExit(f"{name}/{label!r}: disabled wildcard changed text")
print(f"PASS Konsole KeyboardTranslator catalog: {len(cases)}/24")
