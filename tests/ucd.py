# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

"""Shared Unicode Character Database derivations for the test suites."""

import re
from pathlib import Path


UNICODE_ROOT = Path(__file__).resolve().parents[1] / "ext" / "unicode"
UCD_RANGE = re.compile(r"^([0-9A-F]+)(?:\.\.([0-9A-F]+))?\s*;\s*([^#]+?)\s*(?:#|$)")


def property_codepoints(path, wanted):
    result = set()
    for line in path.read_text().splitlines():
        match = UCD_RANGE.match(line)
        if match is None or match.group(3).strip() != wanted:
            continue
        first = int(match.group(1), 16)
        last = int(match.group(2) or match.group(1), 16)
        result.update(range(first, last + 1))
    return result


def host_dependent_formats():
    # The visible format controls - Cf outside Default_Ignorable_Code_Point.
    # libc implementations disagree about their cell width and the terminal
    # follows the libc it runs beside (unicode_width.cpp), so no fixed
    # expectation exists for them and the imported width oracles skip their
    # cases entirely.
    formats = property_codepoints(
        UNICODE_ROOT / "DerivedGeneralCategory-17.0.0.txt", "Cf"
    )
    ignorable = property_codepoints(
        UNICODE_ROOT / "DerivedCoreProperties-17.0.0.txt",
        "Default_Ignorable_Code_Point",
    )
    return formats - ignorable
