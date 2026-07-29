#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

from pathlib import Path

from damage_cases import case_names, source_assertions


ROOT = Path(__file__).resolve().parent
source = tuple(name for name, _expected in source_assertions())
manifest = tuple((ROOT / "damage_file_names.txt").read_text().split())
implemented = case_names()
if len(source) != 17:
    raise SystemExit(f"expected 17 WezTerm damage assertions, found {len(source)}")
if manifest != source or implemented != source:
    raise SystemExit(
        "WezTerm damage catalog mismatch: "
        f"source={source}, manifest={manifest}, implemented={implemented}"
    )
print("PASS WezTerm damage catalog: 17/17")
