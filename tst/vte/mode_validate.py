#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

from pathlib import Path

from mode_cases import case_names, source_case_names


ROOT = Path(__file__).resolve().parent
source = source_case_names()
manifest = tuple((ROOT / "mode_file_names.txt").read_text().split())
implemented = case_names()
if len(source) != 2:
    raise SystemExit(f"expected two VTE mode tests, found {len(source)}")
if source != manifest or source != implemented:
    raise SystemExit(
        "VTE mode catalog mismatch: "
        f"source={source}, manifest={manifest}, implemented={implemented}"
    )
print("PASS VTE mode catalog: 2/2 upstream tests")
