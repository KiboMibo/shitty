#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

from pathlib import Path

from cursor_cases import assertion_names, case_names


ROOT = Path(__file__).resolve().parent
source = assertion_names()
manifest = tuple((ROOT / "cursor_file_names.txt").read_text().split())
implemented = case_names()
if len(source) != 64:
    raise SystemExit(f"expected 64 WezTerm cursor assertions, found {len(source)}")
if manifest != source or implemented != source:
    raise SystemExit(
        "WezTerm cursor catalog mismatch: "
        f"source={source}, manifest={manifest}, implemented={implemented}"
    )
print("PASS WezTerm cursor catalog: 64/64")
