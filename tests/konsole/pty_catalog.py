#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "upstream" / "PtyTest.cpp"
METHOD = re.compile(r"^void PtyTest::(test[A-Z][A-Za-z0-9]*)\(\)", re.MULTILINE)
APPLICABLE = ("testWindowSize", "testRunProgram")
NONAPPLICABLE = {
    "testFlowControl": "Konsole flow-control configuration setter/getter",
    "testEraseChar": "Konsole erase-character configuration setter/getter",
    "testUseUtmp": "Konsole utmp configuration setter/getter",
}


def upstream_methods():
    return tuple(METHOD.findall(SOURCE.read_text()))


def case_names():
    return APPLICABLE
