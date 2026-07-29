#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

from pathlib import Path

from pty_catalog import NONAPPLICABLE, case_names, upstream_methods


ROOT = Path(__file__).resolve().parent
manifest = tuple((ROOT / "pty_file_names.txt").read_text().split())
methods = upstream_methods()
expected = case_names()
if len(methods) != 5 or len(set(methods)) != 5:
    raise SystemExit(f"expected 5 unique Konsole PtyTest methods, found {methods}")
classified = set(expected) | set(NONAPPLICABLE)
if classified != set(methods):
    raise SystemExit(
        "Konsole PtyTest classification mismatch: "
        f"missing={sorted(set(methods) - classified)}, "
        f"extra={sorted(classified - set(methods))}"
    )
if manifest != expected:
    raise SystemExit(
        "Konsole PtyTest catalog mismatch: "
        f"missing={sorted(set(expected) - set(manifest))}, "
        f"extra={sorted(set(manifest) - set(expected))}"
    )
print(
    "PASS Konsole PtyTest catalog: "
    f"{len(expected)} applicable, {len(NONAPPLICABLE)} Konsole-only"
)
