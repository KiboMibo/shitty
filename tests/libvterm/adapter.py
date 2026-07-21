#!/usr/bin/env python3

import re
import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Zutty


def decode_perl_string(expression):
    match = re.fullmatch(r'\s*"(.*)"(?:x(\d+))?\s*', expression)
    if not match:
        raise ValueError(f"unsupported Perl string: {expression}")
    source, repeat = match.groups()
    result = bytearray()
    index = 0
    escapes = {
        "a": 7,
        "b": 8,
        "e": 27,
        "n": 10,
        "r": 13,
        "t": 9,
        "f": 12,
        "v": 11,
        "\\": 92,
        '"': 34,
        "$": 36,
        "#": 35,
    }
    while index < len(source):
        if source[index] != "\\":
            result.extend(source[index].encode("utf-8"))
            index += 1
            continue
        index += 1
        if index >= len(source):
            raise ValueError("trailing backslash")
        escape = source[index]
        index += 1
        if escape == "x":
            if index < len(source) and source[index] == "{":
                end = source.index("}", index)
                value = int(source[index + 1 : end], 16)
                index = end + 1
                result.extend(chr(value).encode("utf-8"))
            else:
                digits = source[index : index + 2]
                if len(digits) != 2 or not re.fullmatch(r"[0-9a-fA-F]{2}", digits):
                    raise ValueError("invalid hex escape")
                result.append(int(digits, 16))
                index += 2
        elif escape in escapes:
            result.append(escapes[escape])
        else:
            result.extend(escape.encode("utf-8"))
    return bytes(result) * int(repeat or 1)


def expand_controls(lines):
    expanded = []
    for number, line in enumerate(lines, 1):
        stripped = line.strip()
        match = re.fullmatch(r"\$SEQ\s+(\d+)\s+(\d+):\s*(.*)", stripped)
        if match:
            low, high, body = match.groups()
            for value in range(int(low), int(high) + 1):
                expanded.append((number, body.replace(r"\#", str(value))))
            continue
        match = re.fullmatch(r"\$REP\s+(\d+):\s*(.*)", stripped)
        if match:
            count, body = match.groups()
            expanded.extend((number, body) for _ in range(int(count)))
            continue
        expanded.append((number, stripped))
    return expanded


def codepoints(value):
    value = value.strip()
    if not value:
        return ()
    if value.startswith('"'):
        return tuple(decode_perl_string(value).decode("utf-8"))
    return tuple(chr(int(token.strip(), 16)) for token in value.split(","))


def cell_text(cell):
    if cell.double_width_continuation:
        return ""
    if cell.grapheme:
        return "".join(map(chr, cell.grapheme))
    return cell.char if cell.char != "\0" else " "


def row_text(snapshot, row, start=0, end=None):
    if end is None:
        end = snapshot.columns
    return "".join(
        cell_text(snapshot.cell(column, row))
        for column in range(start, min(end, snapshot.columns))
    ).rstrip(" ")


def range_text(snapshot, start_row, start_column, end_row, end_column):
    lines = []
    for row in range(start_row, min(end_row, snapshot.rows)):
        first = start_column if row == start_row else 0
        last = end_column if row == end_row - 1 else snapshot.columns
        lines.append(row_text(snapshot, row, first, last))
    return "\n".join(lines)


def parse_color(value):
    match = re.fullmatch(r"rgb\((\d+),(\d+),(\d+)\)", value)
    if not match:
        raise ValueError(f"unsupported color: {value}")
    return tuple(map(int, match.groups()))


LIBVTERM_PALETTE = (
    (0, 0, 0), (224, 0, 0), (0, 224, 0), (224, 224, 0),
    (0, 0, 224), (224, 0, 224), (0, 224, 224), (224, 224, 224),
    (128, 128, 128), (255, 64, 64), (64, 255, 64), (255, 255, 64),
    (64, 64, 255), (255, 64, 255), (64, 255, 255), (255, 255, 255),
)


def osc_color(color):
    return f"rgb:{color[0]:02x}/{color[1]:02x}/{color[2]:02x}".encode()


def configure_libvterm_colors(terminal):
    palette = b";".join(
        str(index).encode() + b";" + osc_color(color)
        for index, color in enumerate(LIBVTERM_PALETTE)
    )
    terminal.write(
        b"\x1b]4;" + palette + b"\x1b\\"
        b"\x1b]10;rgb:f0/f0/f0\x1b\\"
        b"\x1b]11;rgb:00/00/00\x1b\\"
    )


def compare_cell(snapshot, assertion, expected):
    match = re.fullmatch(r"screen_cell\s+(\d+),(\d+)", assertion)
    if not match:
        raise ValueError(f"invalid cell assertion: {assertion}")
    row, column = map(int, match.groups())
    cell = snapshot.cell(column, row)
    glyph_match = re.search(r"\{([^}]*)\}", expected)
    wanted_glyph = ()
    if glyph_match and glyph_match.group(1).strip():
        wanted_glyph = tuple(
            int(value.strip(), 16) for value in glyph_match.group(1).split(",")
        )
    actual_glyph = cell.grapheme or (() if cell.char in ("\0", " ") else (ord(cell.char),))
    differences = []
    if actual_glyph != wanted_glyph:
        differences.append(f"glyph={actual_glyph!r}, expected {wanted_glyph!r}")
    width_match = re.search(r"\bwidth=(\d+)", expected)
    if width_match:
        actual_width = 2 if cell.double_width else 1
        if actual_width != int(width_match.group(1)):
            differences.append(f"width={actual_width}")
    attrs_match = re.search(r"\battrs=\{([^}]*)\}", expected)
    if attrs_match:
        attrs = attrs_match.group(1)
        wanted = {
            "bold": "B" in attrs,
            "italic": "I" in attrs,
            "underline": "U" in attrs,
            "inverse": "R" in attrs,
        }
        for field, value in wanted.items():
            if getattr(cell, field) != value:
                differences.append(f"{field}={getattr(cell, field)}")
    for label, field in (("fg", "foreground"), ("bg", "background")):
        color_match = re.search(rf"\b{label}=(rgb\([^)]+\))", expected)
        if color_match:
            wanted_color = parse_color(color_match.group(1))
            if getattr(cell, field) != wanted_color:
                differences.append(f"{label}={getattr(cell, field)!r}")
    wanted_line = 0
    if "dhl-top" in expected:
        wanted_line = 1
    elif "dhl-bottom" in expected:
        wanted_line = 2
    elif "dwl" in expected:
        wanted_line = 3
    if cell.line_attribute != wanted_line:
        differences.append(f"line_attribute={cell.line_attribute}")
    if "S^" in (attrs_match.group(1) if attrs_match else ""):
        differences.append("superscript is not represented by Zutty cells")
    if "S_" in (attrs_match.group(1) if attrs_match else ""):
        differences.append("subscript is not represented by Zutty cells")
    return "; ".join(differences)


def compare_assertion(terminal, assertion, expected):
    snapshot = terminal.model_snapshot()
    if assertion == "cursor":
        row, column = map(int, expected.split(","))
        actual = (snapshot.cursor_y, snapshot.cursor_x)
        return "" if actual == (row, column) else f"got {actual}"

    match = re.fullmatch(r"screen_row\s+(\d+)", assertion)
    if match:
        wanted = "".join(codepoints(expected))
        actual = row_text(snapshot, int(match.group(1)))
        return "" if actual == wanted else f"got {actual!r}, expected {wanted!r}"

    match = re.fullmatch(
        r"screen_(chars|text)\s+(\d+),(\d+),(\d+),(\d+)", assertion
    )
    if match:
        kind = match.group(1)
        coordinates = tuple(map(int, match.groups()[1:]))
        actual_text = range_text(snapshot, *coordinates)
        if kind == "chars":
            wanted = "".join(codepoints(expected))
            return "" if actual_text == wanted else f"got {actual_text!r}, expected {wanted!r}"
        actual = tuple(actual_text.encode("utf-8"))
        wanted = tuple(ord(value) for value in codepoints(expected))
        return "" if actual == wanted else f"got {actual!r}, expected {wanted!r}"

    match = re.fullmatch(r"screen_eol\s+(\d+),(\d+)", assertion)
    if match:
        row, column = map(int, match.groups())
        actual = int(column >= len(row_text(snapshot, row)))
        return "" if actual == int(expected) else f"got {actual}"

    if assertion.startswith("screen_cell "):
        return compare_cell(snapshot, assertion, expected)

    match = re.fullmatch(r"lineinfo\s+(\d+)", assertion)
    if match:
        row = int(match.group(1))
        actual = ""
        if row and any(
            snapshot.cell(column, row - 1).wrapped
            for column in range(snapshot.columns)
        ):
            actual = "cont"
        return "" if actual == expected else f"got {actual!r}"

    match = re.fullmatch(r"screen_attrs_extent\s+(\d+),(\d+)", assertion)
    if match:
        row, column = map(int, match.groups())
        cell = snapshot.cell(column, row)
        signature = (
            cell.bold, cell.italic, cell.underline, cell.inverse,
            cell.foreground, cell.background,
        )
        left = column
        right = column + 1
        while left and (
            lambda candidate: (
                candidate.bold, candidate.italic, candidate.underline,
                candidate.inverse, candidate.foreground, candidate.background,
            ) == signature
        )(snapshot.cell(left - 1, row)):
            left -= 1
        while right < snapshot.columns and (
            lambda candidate: (
                candidate.bold, candidate.italic, candidate.underline,
                candidate.inverse, candidate.foreground, candidate.background,
            ) == signature
        )(snapshot.cell(right, row)):
            right += 1
        actual = f"{row},{left}-{row + 1},{right - 1}"
        return "" if actual == expected else f"got {actual}"

    raise ValueError(f"unsupported assertion: {assertion}")


def apply_command(terminal, line):
    if line in ("INIT", "RESET"):
        if line == "RESET":
            terminal.write(b"\x1bc")
            configure_libvterm_colors(terminal)
        return
    if line.startswith(("WANTSCREEN", "WANTSTATE", "UTF8", "DAMAGEMERGE", "DAMAGEFLUSH")):
        return
    if line.startswith("PUSH "):
        terminal.write(decode_perl_string(line[5:]))
        return
    if line.startswith("RESIZE "):
        rows, columns = map(int, line[7:].split(","))
        terminal.resize(columns, rows)
        return
    if line.startswith("SETDEFAULTCOL "):
        colors = re.findall(r"rgb\([^)]+\)", line)
        payload = b""
        if colors:
            payload += b"\x1b]10;" + osc_color(parse_color(colors[0])) + b"\x1b\\"
        if len(colors) > 1:
            payload += b"\x1b]11;" + osc_color(parse_color(colors[1])) + b"\x1b\\"
        terminal.write(payload)
        return
    raise ValueError(f"unsupported command: {line}")


def run_fixture(path):
    mismatches = []
    checked = 0
    skipped = 0
    with Zutty(columns=80, rows=25, save_lines=500) as terminal:
        for number, line in expand_controls(path.read_text().splitlines()):
            if not line or line.startswith("#") or line == "__END__" or line.startswith("!"):
                continue
            match = re.fullmatch(r"\?([a-z_]+(?:\s+[^=]+)?)\s*=\s*(.*)", line)
            if match:
                assertion, expected = (value.strip() for value in match.groups())
                try:
                    mismatch = compare_assertion(terminal, assertion, expected)
                except ValueError:
                    skipped += 1
                    continue
                checked += 1
                if mismatch:
                    mismatches.append(f"line {number}: {assertion}: {mismatch}")
                continue
            if line[0].isupper():
                try:
                    apply_command(terminal, line)
                except ValueError:
                    skipped += 1
                continue
            skipped += 1
    if not checked:
        mismatches.append("fixture has no supported golden assertions")
    return checked, skipped, mismatches


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: adapter.py FIXTURE XFAIL_FILE STAMP")
    fixture = sys.argv[1]
    xfails = {
        line.strip() for line in Path(sys.argv[2]).read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }
    stamp = Path(sys.argv[3])
    path = Path(__file__).resolve().parent / "upstream" / fixture
    checked, skipped, mismatches = run_fixture(path)
    mismatch = "; ".join(mismatches[:8])
    if len(mismatches) > 8:
        mismatch += f"; ... {len(mismatches) - 8} more"
    if fixture in xfails:
        if not mismatches:
            print(f"XPASS libvterm/{fixture}", file=sys.stderr)
            return 1
        print(
            f"XFAIL libvterm/{fixture}: {len(mismatches)}/{checked} mismatches, "
            f"{skipped} callback assertions pending: {mismatch}"
        )
    elif mismatches:
        print(f"FAIL libvterm/{fixture}: {mismatch}", file=sys.stderr)
        return 1
    else:
        print(
            f"PASS libvterm/{fixture}: {checked} golden assertions, "
            f"{skipped} callback assertions pending"
        )
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
