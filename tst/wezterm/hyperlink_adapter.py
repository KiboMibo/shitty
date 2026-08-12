#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Shitty
from hyperlink_cases import FIRST, SECOND, case_data


def default_attributes(cell):
    return (
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
        cell.foreground,
        cell.background,
        cell.underline_color,
        cell.semantic,
        cell.protected,
        cell.line_attribute,
        cell.grapheme,
    )


DEFAULT_ATTRIBUTES = (
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
    (255, 255, 255),
    (0, 0, 0),
    (255, 255, 255),
    0,
    False,
    0,
    (),
)


def verify_case(terminal, snapshot, expected_text, expected_links):
    expected_lines = [line.ljust(snapshot.columns) for line in expected_text]
    if snapshot.lines != expected_lines:
        raise AssertionError(
            f"expected text={expected_lines!r}, got={snapshot.lines!r}"
        )

    identities = {}
    expected_uris = {"A": FIRST, "B": SECOND}
    for row, (text, links) in enumerate(zip(expected_text, expected_links)):
        if len(text) != len(links):
            raise AssertionError(
                f"bad translated oracle on row {row}: {text!r} vs {links!r}"
            )
        for column, alias in enumerate(links):
            cell = snapshot.cell(column, row)
            if default_attributes(cell) != DEFAULT_ATTRIBUTES:
                raise AssertionError(
                    f"non-default attributes at ({column}, {row}): {cell}"
                )
            if alias == ".":
                if cell.hyperlink != 0:
                    raise AssertionError(
                        f"unexpected hyperlink at ({column}, {row})"
                    )
                continue
            if cell.hyperlink == 0:
                raise AssertionError(
                    f"missing hyperlink {alias} at ({column}, {row})"
                )
            previous = identities.setdefault(alias, cell.hyperlink)
            if previous != cell.hyperlink:
                raise AssertionError(
                    f"hyperlink {alias} changed identity at ({column}, {row})"
                )
            uri = terminal.hyperlink_bytes(column, row)
            if uri != expected_uris[alias]:
                raise AssertionError(
                    f"hyperlink {alias} URI at ({column}, {row}): {uri!r}"
                )
    if len(set(identities.values())) != len(identities):
        raise AssertionError(f"distinct hyperlinks alias one identity: {identities}")


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: hyperlink_adapter.py CASE STAMP")
    name = sys.argv[1]
    stamp = Path(sys.argv[2])
    label, payload, expected_text, expected_links = case_data(name)
    with Shitty(columns=5, rows=3, save_lines=0) as terminal:
        terminal.write(payload)
        snapshot = terminal.model_snapshot()
        verify_case(terminal, snapshot, expected_text, expected_links)
    print(f"PASS WezTerm hyperlink/{name} ({label})")
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
