#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

from pathlib import Path

from semantic_cases import case_names


ROOT = Path(__file__).resolve().parent
listed = tuple((ROOT / "semantic_file_names.txt").read_text().split())
implemented = case_names()
if len(listed) != 32:
    raise SystemExit(f"expected 32 libtsm cases, found {len(listed)}")
if len(set(listed)) != len(listed):
    raise SystemExit("duplicate libtsm case names")
if listed != implemented:
    missing = sorted(set(listed) - set(implemented))
    extra = sorted(set(implemented) - set(listed))
    raise SystemExit(f"libtsm catalog mismatch: missing={missing}, extra={extra}")
print("PASS libtsm semantic catalog: 32/32")
