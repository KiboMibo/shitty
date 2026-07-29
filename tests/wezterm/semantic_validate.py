#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

from pathlib import Path

from semantic_cases import (
    case_names,
    source_attribute_assertions,
    source_zone_assertions,
)


ROOT = Path(__file__).resolve().parent
source = tuple(name for name, _zones in source_zone_assertions())
source += source_attribute_assertions()
manifest = tuple((ROOT / "semantic_file_names.txt").read_text().split())
implemented = case_names()
if len(source) != 4:
    raise SystemExit(
        f"expected four WezTerm semantic assertions, found {len(source)}"
    )
if manifest != source or implemented != source:
    raise SystemExit(
        "WezTerm semantic catalog mismatch: "
        f"source={source}, manifest={manifest}, implemented={implemented}"
    )
print("PASS WezTerm semantic catalog: 3/3 zones, 1/1 cell attributes")
