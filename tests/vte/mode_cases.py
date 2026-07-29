# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "upstream" / "modes-test.cc"
REGISTRATION = re.compile(
    r'g_test_add_func\("/vte/modes/([^"]+)",\s*test_modes_[a-z_]+\);'
)


def source_case_names():
    return tuple(REGISTRATION.findall(SOURCE.read_text()))


def case_names():
    return ("ecma", "private")
