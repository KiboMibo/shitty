#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

from pathlib import Path

from tabstop_cases import case_names, source_case_names


ROOT = Path(__file__).resolve().parent
source = source_case_names()
manifest = tuple((ROOT / "tabstop_file_names.txt").read_text().split())
implemented = case_names()
if len(source) != 7:
    raise SystemExit(f"expected seven VTE tabstop tests, found {len(source)}")
if source != manifest or source != implemented:
    raise SystemExit(
        "VTE tabstop catalog mismatch: "
        f"source={source}, manifest={manifest}, implemented={implemented}"
    )
print("PASS VTE tabstop catalog: 7/7 upstream tests")
