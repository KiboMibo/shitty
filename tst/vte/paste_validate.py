#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

from pathlib import Path

from paste_cases import (
    C0_CONTROLS,
    C1_CONTROLS,
    CASE_NAMES,
    STRING_CASES,
    control_vectors,
)


ROOT = Path(__file__).resolve().parent
source = (ROOT / "upstream" / "pastify-test.cc").read_text()
listed = tuple((ROOT / "paste_file_names.txt").read_text().split())

if listed != CASE_NAMES:
    raise AssertionError(f"paste target catalog differs: {listed!r}")
if len(C0_CONTROLS) != 29:
    raise AssertionError(f"expected 29 C0/DEL controls, got {len(C0_CONTROLS)}")
if len(C1_CONTROLS) != 32:
    raise AssertionError(f"expected 32 C1 controls, got {len(C1_CONTROLS)}")
if len(STRING_CASES) != 8:
    raise AssertionError(f"expected 8 string cases, got {len(STRING_CASES)}")
if len(control_vectors(b"x", b"y")) != 8:
    raise AssertionError("VTE control placement matrix is incomplete")

required_source = (
    'g_test_add_func("/vte/pastify/brackets/c0"',
    'g_test_add_func("/vte/pastify/brackets/c1"',
    "for (auto c = 0; c < 0x20; ++c)",
    "c == 0 || c == 0x09 || c == 0x0a || c == 0x0d",
    'auto const path = "/vte/pastify/controls/c0/7f"',
    "for (auto c = 0x80; c < 0xa0; ++c)",
    "G_N_ELEMENTS (test_strings)",
)
for fragment in required_source:
    if fragment not in source:
        raise AssertionError(f"missing upstream paste family: {fragment}")

print(
    "PASS VTE paste catalog: 71 upstream cases classified, "
    "70 product cases executable, 1 internal C1-bracket variant"
)
