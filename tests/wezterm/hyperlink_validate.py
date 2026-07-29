#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

from pathlib import Path

from hyperlink_cases import case_names, source_assertions


ROOT = Path(__file__).resolve().parent
source = source_assertions()
manifest = tuple((ROOT / "hyperlink_file_names.txt").read_text().split())
implemented = case_names()
if len(source) != 3:
    raise SystemExit(
        f"expected three WezTerm hyperlink assertions, found {len(source)}"
    )
if manifest != source or implemented != source:
    raise SystemExit(
        "WezTerm hyperlink catalog mismatch: "
        f"source={source}, manifest={manifest}, implemented={implemented}"
    )
print("PASS WezTerm hyperlink catalog: 3/3 attribute assertions")
