# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "upstream" / "tabstops-test.cc"
REGISTRATION = re.compile(
    r'g_test_add_func\("/vte/tabstops/([^"]+)",\s*test_tabstops_[a-z_]+\);'
)


def source_case_names():
    return tuple(REGISTRATION.findall(SOURCE.read_text()))


def case_names():
    return (
        "default",
        "get-set",
        "clear",
        "reset",
        "resize",
        "previous",
        "next",
    )
