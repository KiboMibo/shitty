#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

from pathlib import Path

from color_cases import case_names, named_colors, source_case_names


ROOT = Path(__file__).resolve().parent
source = tuple(name.replace("/", "-") for name in source_case_names())
manifest = tuple((ROOT / "color_file_names.txt").read_text().split())
implemented = case_names()
if len(source) != 5:
    raise SystemExit(f"expected five VTE color tests, found {len(source)}")
if len(named_colors()) != 782:
    raise SystemExit(
        f"expected 782 VTE named-color vectors, found {len(named_colors())}"
    )
if source != manifest or source != implemented:
    raise SystemExit(
        "VTE color catalog mismatch: "
        f"source={source}, manifest={manifest}, implemented={implemented}"
    )
print("PASS VTE color catalog: 5/5 upstream tests, 782 named vectors")
