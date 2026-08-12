#!/usr/bin/env python3

import json
from pathlib import Path


FORMAT_VERSION = 1


def cell_record(cell):
    flags = 0
    for bit, value in enumerate((
        cell.double_width,
        cell.double_width_continuation,
        cell.bold,
        cell.italic,
        cell.underline,
        cell.faint,
        cell.blink,
        cell.conceal,
        cell.strike,
        cell.overline,
        cell.inverse,
        cell.wrapped,
        cell.protected,
        cell.drawn,
    )):
        flags |= int(value) << bit
    return [
        ord(cell.char),
        list(cell.grapheme),
        flags,
        cell.underline_style,
        list(cell.foreground),
        list(cell.background),
        list(cell.underline_color),
        cell.foreground_index,
        cell.background_index,
        cell.underline_index,
        cell.hyperlink,
        cell.semantic,
        cell.line_attribute,
    ]


def run_length_encode(records):
    runs = []
    for record in records:
        if runs and runs[-1][1] == record:
            runs[-1][0] += 1
        else:
            runs.append([1, record])
    return runs


def canonical_snapshot(snapshot, render_state):
    return {
        "format": FORMAT_VERSION,
        "geometry": [snapshot.columns, snapshot.rows],
        "cursor": [snapshot.cursor_x, snapshot.cursor_y, snapshot.cursor_style],
        "view_offset": snapshot.view_offset,
        "selection": [*snapshot.selection, int(snapshot.rectangular_selection)],
        "render": {
            "screen_reverse": int(render_state.screen_reverse),
            "selection_mask": render_state.selection_mask,
            "selection_foreground": list(render_state.selection_foreground),
            "selection_background": list(render_state.selection_background),
            "grapheme_cells": render_state.grapheme_cells,
            "grapheme_codepoints": render_state.grapheme_codepoints,
        },
        "cells_rle": run_length_encode(map(cell_record, snapshot.cells)),
    }


def encode_snapshot(snapshot):
    return json.dumps(
        snapshot,
        ensure_ascii=False,
        indent=2,
        sort_keys=True,
    ) + "\n"


def snapshot_text(snapshot):
    columns, rows = snapshot["geometry"]
    cells = []
    for count, record in snapshot["cells_rle"]:
        cells.extend(chr(record[0]) for _ in range(count))
    return "\n".join(
        "".join(cells[row * columns:(row + 1) * columns]).rstrip()
        for row in range(rows)
    )


def color_style_count(snapshot):
    styles = set()
    for _count, record in snapshot["cells_rle"]:
        if record[2] & (1 << 13):
            styles.add((
                tuple(record[4]), tuple(record[5]),
                record[7], record[8],
            ))
    return len(styles)


def verify_snapshot_contract(snapshot, expected_text, minimum_color_styles):
    text = snapshot_text(snapshot)
    for fragment in expected_text:
        if fragment not in text:
            raise ValueError(f"snapshot does not contain {fragment!r}")
    styles = color_style_count(snapshot)
    if styles < minimum_color_styles:
        raise ValueError(
            f"snapshot has {styles} color styles, expected at least "
            f"{minimum_color_styles}"
        )


def read_cases(path):
    cases = json.loads(Path(path).read_text())
    if not isinstance(cases, dict):
        raise ValueError("real-world case manifest must be an object")
    return cases
