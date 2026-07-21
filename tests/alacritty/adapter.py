#!/usr/bin/env python3

import json
import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Zutty


NAMED_COLORS = {
    "Black": 0,
    "Red": 1,
    "Green": 2,
    "Yellow": 3,
    "Blue": 4,
    "Magenta": 5,
    "Cyan": 6,
    "White": 7,
    "BrightBlack": 8,
    "BrightRed": 9,
    "BrightGreen": 10,
    "BrightYellow": 11,
    "BrightBlue": 12,
    "BrightMagenta": 13,
    "BrightCyan": 14,
    "BrightWhite": 15,
}


def flags(value):
    return set(value.split(" | ")) if value else set()


def expected_color(value, channel, bold=False):
    if "Spec" in value:
        color = value["Spec"]
        return ("rgb", color["r"], color["g"], color["b"])
    if "Indexed" in value:
        index = value["Indexed"]
    else:
        name = value["Named"]
        if name == "Foreground":
            return ("default_foreground",)
        if name == "Background":
            return ("default_background",)
        index = NAMED_COLORS[name]
    if channel == "foreground" and bold and index < 8:
        index += 8
    return ("indexed", index)


def actual_color(index, color):
    if index >= 0:
        return ("indexed", index)
    if index == -1:
        return ("rgb", *color)
    if color == (255, 255, 255):
        return ("default_foreground",)
    if color == (0, 0, 0):
        return ("default_background",)
    return ("default_rgb", *color)


def expected_cell(cell):
    cell_flags = flags(cell["flags"])
    extra = cell["extra"] or {}
    foreground = expected_color(
        cell["fg"], "foreground", "BOLD" in cell_flags
    )
    background = expected_color(cell["bg"], "background")
    if "INVERSE" in cell_flags:
        foreground, background = background, foreground

    underline_value = extra.get("underline_color")
    underline = (
        expected_color(underline_value, "underline")
        if underline_value is not None else foreground
    )
    hyperlink = extra.get("hyperlink")
    hyperlink = hyperlink["inner"]["uri"] if hyperlink else ""

    underline_styles = (
        ("DASHED_UNDERLINE", 5),
        ("DOTTED_UNDERLINE", 4),
        ("UNDERCURL", 3),
        ("DOUBLE_UNDERLINE", 2),
        ("UNDERLINE", 1),
    )
    underline_style = next(
        (style for name, style in underline_styles if name in cell_flags), 0
    )
    continuation = "WIDE_CHAR_SPACER" in cell_flags
    grapheme = () if continuation else tuple(map(ord, cell["c"]))
    grapheme += tuple(map(ord, extra.get("zerowidth", ())))

    return {
        "grapheme": grapheme,
        "wide": "WIDE_CHAR" in cell_flags,
        "continuation": continuation,
        "wrap": "WRAPLINE" in cell_flags,
        "bold": "BOLD" in cell_flags,
        "italic": "ITALIC" in cell_flags,
        "underline": underline_style,
        "faint": "DIM" in cell_flags,
        "blink": False,
        "conceal": "HIDDEN" in cell_flags,
        "strike": "STRIKEOUT" in cell_flags,
        "overline": False,
        "foreground": foreground,
        "background": background,
        "underline_color": underline,
        "hyperlink": hyperlink,
    }


def actual_cell(cell, hyperlink):
    foreground = actual_color(cell.foreground_index, cell.foreground)
    background = actual_color(cell.background_index, cell.background)
    if cell.inverse:
        foreground, background = background, foreground
    grapheme = () if cell.double_width_continuation else (
        cell.grapheme or (ord(cell.char),)
    )
    return {
        "grapheme": grapheme,
        "wide": cell.double_width,
        "continuation": cell.double_width_continuation,
        "wrap": cell.wrapped,
        "bold": cell.bold,
        "italic": cell.italic,
        "underline": cell.underline_style,
        "faint": cell.faint,
        "blink": cell.blink,
        "conceal": cell.conceal,
        "strike": cell.strike,
        "overline": cell.overline,
        "foreground": foreground,
        "background": background,
        "underline_color": actual_color(
            cell.underline_index, cell.underline_color
        ),
        "hyperlink": hyperlink,
    }


def expected_grid(serialized):
    raw = serialized["raw"]
    if raw["zero"] != 0:
        raise RuntimeError("Alacritty golden grid is not zeroed")
    rows = reversed(raw["inner"][:raw["len"]])
    return [
        [expected_cell(cell) for cell in row["inner"]]
        for row in rows
    ]


def actual_grid(terminal):
    views = []
    previous_offset = None
    while True:
        snapshot = terminal.model_snapshot()
        if snapshot.view_offset == previous_offset:
            break
        previous_offset = snapshot.view_offset
        links = {}
        for index, cell in enumerate(snapshot.cells):
            if cell.hyperlink:
                row, column = divmod(index, snapshot.columns)
                links[index] = terminal.hyperlink(column, row)
        views.append((snapshot, links))
        terminal.page_up()

    history = max(snapshot.view_offset for snapshot, _ in views)
    rows = [None] * (history + views[0][0].rows)
    for snapshot, links in views:
        first_row = history - snapshot.view_offset
        for row in range(snapshot.rows):
            start = row * snapshot.columns
            rows[first_row + row] = [
                actual_cell(cell, links.get(start + column, ""))
                for column, cell in enumerate(
                    snapshot.cells[start : start + snapshot.columns]
                )
            ]
    if any(row is None for row in rows):
        raise RuntimeError("incomplete Zutty scrollback snapshot")
    return rows


def compare(case_path):
    size = json.loads((case_path / "size.json").read_text())
    config = json.loads((case_path / "config.json").read_text())
    expected = expected_grid(json.loads((case_path / "grid.json").read_text()))

    with Zutty(
        columns=size["columns"],
        rows=size["screen_lines"],
        save_lines=config["history_size"],
    ) as terminal:
        terminal.write((case_path / "alacritty.recording").read_bytes())
        actual = actual_grid(terminal)

    differences = []
    if len(expected) != len(actual):
        differences.append(
            f"row count: expected {len(expected)}, actual {len(actual)}"
        )
    for row in range(min(len(expected), len(actual))):
        if len(expected[row]) != len(actual[row]):
            differences.append(
                f"row {row} width: expected {len(expected[row])}, "
                f"actual {len(actual[row])}"
            )
            continue
        for column, (wanted, got) in enumerate(zip(expected[row], actual[row])):
            if wanted != got:
                differences.append(
                    f"[{row}][{column}] expected {wanted!r}\n"
                    f"           actual {got!r}"
                )
                if len(differences) == 5:
                    return differences
    return differences


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: adapter.py CASE XFAIL_FILE STAMP")
    case = sys.argv[1]
    xfail_path = Path(sys.argv[2])
    stamp = Path(sys.argv[3])
    known_failures = {
        line.strip()
        for line in xfail_path.read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }

    try:
        differences = compare(Path(__file__).resolve().parent / case)
    except Exception as error:
        differences = [f"adapter/replay error: {error}"]

    if case in known_failures:
        if not differences:
            print(f"XPASS Alacritty/{case}", file=sys.stderr)
            return 1
        print(f"XFAIL Alacritty/{case}")
    elif differences:
        print(f"FAIL Alacritty/{case}", file=sys.stderr)
        print("\n".join(differences), file=sys.stderr)
        return 1
    else:
        print(f"PASS Alacritty/{case}")

    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
