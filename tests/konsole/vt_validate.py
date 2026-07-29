#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

from pathlib import Path

from vt_cases import case_names


ROOT = Path(__file__).resolve().parent
listed = tuple((ROOT / "vt_file_names.txt").read_text().split())
implemented = case_names()
if len(listed) != 11:
    raise SystemExit(f"expected 11 Konsole Vt102 cases, found {len(listed)}")
if len(set(listed)) != len(listed):
    raise SystemExit("duplicate Konsole Vt102 case names")
if listed != implemented:
    raise SystemExit(
        "Konsole Vt102 catalog mismatch: "
        f"missing={sorted(set(listed) - set(implemented))}, "
        f"extra={sorted(set(implemented) - set(listed))}"
    )
print("PASS Konsole Vt102 semantic catalog: 11/11")
