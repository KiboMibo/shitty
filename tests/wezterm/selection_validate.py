#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import re
from pathlib import Path

from selection_cases import case_names


ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "upstream" / "selection.rs"
ASSERTION = re.compile(
    r"assert_eq!\s*\(\s*term\.get_clipboard\(\)\.unwrap\(\)",
    re.MULTILINE,
)
source = tuple(
    f"selection_{SOURCE.read_text().count(chr(10), 0, match.start()) + 1:04d}"
    for match in ASSERTION.finditer(SOURCE.read_text())
)
manifest = tuple((ROOT / "selection_file_names.txt").read_text().split())
implemented = case_names()
known = {
    line.strip() for line in (ROOT / "selection_xfail.txt").read_text().splitlines()
    if line.strip() and not line.startswith("#")
}
if len(source) != 12:
    raise SystemExit(f"expected 12 WezTerm clipboard assertions, found {len(source)}")
if manifest != source or implemented != source:
    raise SystemExit(
        "WezTerm selection catalog mismatch: "
        f"source={source}, manifest={manifest}, implemented={implemented}"
    )
if not known <= set(manifest):
    raise SystemExit(f"unknown WezTerm selection XFAIL entries: {sorted(known - set(manifest))}")
print("PASS WezTerm selection catalog: 12/12")
