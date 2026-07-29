# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "upstream" / "color-test.cc"
NAMES_SOURCE = ROOT / "upstream" / "color-names-tests.hh"
REGISTRATION = re.compile(
    r'g_test_add_func\("/vte/color/([^"]+)",\s*test_color_[a-z0-9_]+\);'
)
NAMED = re.compile(
    r'assert_color_parse_named\("([^"]+)", 0x([0-9a-f]{6})\);'
)


def source_case_names():
    return tuple(REGISTRATION.findall(SOURCE.read_text()))


def case_names():
    return (
        "parse-css",
        "parse-x11",
        "parse-named",
        "parse-nothing",
        "to-string",
    )


def named_colors():
    return tuple(
        (name, int(rgb, 16))
        for name, rgb in NAMED.findall(NAMES_SOURCE.read_text())
    )
