#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

from pathlib import Path

from utf8_cases import case_names, replacement_vectors


ROOT = Path(__file__).resolve().parent
manifest = tuple((ROOT / "utf8_file_names.txt").read_text().split())
if manifest != case_names():
    raise SystemExit(
        f"VTE UTF-8 manifest mismatch: manifest={manifest}, "
        f"implemented={case_names()}"
    )
if len(replacement_vectors()) != 108:
    raise SystemExit(
        f"expected 108 VTE replacement vectors, "
        f"found {len(replacement_vectors())}"
    )
print("PASS VTE UTF-8 catalog: all scalars + 108 replacement vectors")
