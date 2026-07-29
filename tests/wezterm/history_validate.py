#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

from pathlib import Path

from history_cases import (
    CASES,
    case_names,
    source_assertions,
    source_stable_assertions,
)


ROOT = Path(__file__).resolve().parent
source = tuple(name for name, _expected in source_assertions())
manifest = tuple((ROOT / "history_file_names.txt").read_text().split())
implemented = case_names()
if len(source) != 27:
    raise SystemExit(
        f"expected 27 WezTerm all-contents assertions, found {len(source)}"
    )
if manifest != source or implemented != source:
    raise SystemExit(
        "WezTerm history catalog mismatch: "
        f"source={source}, manifest={manifest}, implemented={implemented}"
    )
stable_source = source_stable_assertions()
stable_implemented = tuple(
    item
    for case in CASES
    for item in case[-1]
)
if len(stable_source) != 20 or stable_implemented != stable_source:
    raise SystemExit(
        "WezTerm stable-row catalog mismatch: "
        f"source={stable_source}, implemented={stable_implemented}"
    )
print("PASS WezTerm history catalog: 27/27 contents, 20/20 stable rows")
