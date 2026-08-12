#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import sys
import unicodedata
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Shitty
from metadata_cases import case_data


def verify_plain_cell(cell, background_index):
    actual = (
        cell.double_width,
        cell.double_width_continuation,
        cell.bold,
        cell.italic,
        cell.underline,
        cell.underline_style,
        cell.faint,
        cell.blink,
        cell.conceal,
        cell.strike,
        cell.overline,
        cell.inverse,
        cell.foreground_index,
        cell.background_index,
        cell.semantic,
        cell.protected,
        cell.line_attribute,
        cell.hyperlink,
        cell.grapheme,
    )
    expected = (
        False,
        False,
        False,
        False,
        False,
        0,
        False,
        False,
        False,
        False,
        False,
        False,
        -2,
        background_index,
        0,
        False,
        0,
        0,
        (),
    )
    if actual != expected:
        raise AssertionError(f"expected default cell with BCE, got {cell}")


def verify_cells(snapshot, background_index):
    expected = "foo     " if snapshot.columns == 8 else " " * snapshot.columns
    if any(line != expected for line in snapshot.lines):
        raise AssertionError(
            f"expected {snapshot.rows} copies of {expected!r}, "
            f"got={snapshot.lines!r}"
        )
    for cell in snapshot.cells:
        verify_plain_cell(cell, background_index)


def verify_line(snapshot, case_index, line_attribute):
    row = case_index - 2
    expected_text = ("line1", "line2", "line3", "line4")
    if snapshot.lines != [
        text.ljust(snapshot.columns) for text in expected_text
    ]:
        raise AssertionError(f"unexpected DEC line-mode text: {snapshot.lines}")
    attributes = {
        cell.line_attribute
        for cell in snapshot.cells[
            row * snapshot.columns : (row + 1) * snapshot.columns
        ]
    }
    if attributes != {line_attribute}:
        raise AssertionError(
            f"row {row} expected line attribute {line_attribute}, "
            f"got={attributes}"
        )


def verify_grapheme(snapshot, expected):
    cell = snapshot.cell(0, 0)
    if cell.grapheme != expected or not cell.double_width:
        raise AssertionError(
            f"expected one wide grapheme {expected}, got {cell}"
        )
    if not snapshot.cell(1, 0).double_width_continuation:
        raise AssertionError("wide Hangul grapheme has no continuation cell")


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: metadata_adapter.py CASE STAMP")
    name = sys.argv[1]
    stamp = Path(sys.argv[2])
    case_index, data = case_data(name)
    label, rows, columns, payload, kind, expected = data
    with Shitty(columns=columns, rows=rows, save_lines=0) as terminal:
        terminal.write(payload)
        snapshot = terminal.model_snapshot()
    if kind == "cells":
        verify_cells(snapshot, expected)
    elif kind == "line":
        verify_line(snapshot, case_index, expected)
    elif kind == "nfc":
        source = payload.decode()
        if unicodedata.normalize("NFC", source) != expected:
            raise AssertionError(f"{source!r} does not normalize to {expected!r}")
    elif kind == "grapheme":
        verify_grapheme(snapshot, expected)
    else:
        raise AssertionError(f"unknown metadata oracle {kind}")
    print(f"PASS WezTerm metadata/{name} ({label})")
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
