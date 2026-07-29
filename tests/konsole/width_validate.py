#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

from pathlib import Path

from width_catalog import case_names, width_cases


ROOT = Path(__file__).resolve().parent
manifest = tuple((ROOT / "width_file_names.txt").read_text().split())
expected = case_names()
if len(expected) != 25:
    raise SystemExit(f"expected 25 Konsole width rows, found {len(expected)}")
if len(set(expected)) != len(expected):
    raise SystemExit("duplicate Konsole width codepoints")
if manifest != expected:
    raise SystemExit(
        "Konsole width catalog mismatch: "
        f"missing={sorted(set(expected) - set(manifest))}, "
        f"extra={sorted(set(manifest) - set(expected))}"
    )
print(f"PASS Konsole CharacterWidth catalog: {len(tuple(width_cases()))}/25")
