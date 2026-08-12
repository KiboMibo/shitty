#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

from pathlib import Path

from semantic_cases import case_names


ROOT = Path(__file__).resolve().parent
listed = tuple((ROOT / "semantic_file_names.txt").read_text().split())
implemented = case_names()
if len(listed) != 13:
    raise SystemExit(f"expected 13 Konsole Screen/History cases, found {len(listed)}")
if len(set(listed)) != len(listed):
    raise SystemExit("duplicate Konsole semantic case names")
if listed != implemented:
    raise SystemExit(
        "Konsole semantic catalog mismatch: "
        f"missing={sorted(set(listed) - set(implemented))}, "
        f"extra={sorted(set(implemented) - set(listed))}"
    )
print("PASS Konsole Screen/History catalog: 13/13")
