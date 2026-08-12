#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

from pathlib import Path

from metadata_cases import case_names, source_assertions


ROOT = Path(__file__).resolve().parent
source = source_assertions()
manifest = tuple((ROOT / "metadata_file_names.txt").read_text().split())
implemented = case_names()
if len(source) != 8:
    raise SystemExit(
        f"expected eight WezTerm metadata assertions, found {len(source)}"
    )
if manifest != source or implemented != source:
    raise SystemExit(
        "WezTerm metadata catalog mismatch: "
        f"source={source}, manifest={manifest}, implemented={implemented}"
    )
print(
    "PASS WezTerm metadata catalog: "
    "2/2 cell, 4/4 line, 2/2 Unicode assertions"
)
