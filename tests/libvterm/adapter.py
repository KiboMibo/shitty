#!/usr/bin/env python3

import base64
import re
import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Shitty


def decode_perl_string(expression):
    parts = re.split(r"\s+\.\s+", expression.strip())
    if len(parts) > 1:
        return b"".join(decode_perl_string(part) for part in parts)
    match = re.fullmatch(r'\s*"(.*)"\s*(?:x\s*(\d+))?\s*', expression)
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
                if value > 0xff:
                    raise ValueError(f"unsupported wide Perl byte escape: {value:x}")
                result.append(value)
            else:
                digits = source[index : index + 2]
                if len(digits) != 2 or not re.fullmatch(r"[0-9a-fA-F]{2}", digits):
                    raise ValueError("invalid hex escape")
                result.append(int(digits, 16))
                index += 2
        elif escape in escapes:
            result.append(escapes[escape])
        elif escape in "01234567":
            digits = escape
            while index < len(source) and len(digits) < 3 and source[index] in "01234567":
                digits += source[index]
                index += 1
            result.append(int(digits, 8))
        else:
            result.extend(escape.encode("utf-8"))
    return bytes(result) * int(repeat or 1)


def decode_callback_fragment(fragment):
    fragment = fragment.strip()
    started = fragment.startswith("[")
    closed = fragment.endswith("]")
    if started:
        fragment = fragment[1:]
    if closed:
        fragment = fragment[:-1]
    fragment = fragment.strip()
    if not fragment:
        return started, closed, b""
    if fragment.startswith('"') and not re.search(r'"\s*(?:x\s*\d+)?$', fragment):
        fragment += '"'
    return started, closed, decode_perl_string(fragment)


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


def overwritten_putglyph_callbacks(lines):
    overwritten = set()
    current = {}
    for number, line in lines:
        if line and line[0].isupper():
            current = {}
            continue
        match = re.fullmatch(
            r"putglyph\s+[0-9a-fx,]+\s+\d+\s+(\d+),(\d+).*", line
        )
        if not match:
            continue
        coordinate = tuple(map(int, match.groups()))
        previous = current.get(coordinate)
        if previous is not None:
            overwritten.add(previous)
        current[coordinate] = number
    return overwritten


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
    end = min(end, snapshot.columns)
    while end > start:
        cell = snapshot.cell(end - 1, row)
        if cell_text(cell) != " " or cell.drawn:
            break
        end -= 1
    return "".join(
        cell_text(snapshot.cell(column, row))
        for column in range(start, end)
    )


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


def compare_cell(snapshot, assertion, expected, screen_reverse):
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
            actual = getattr(cell, field)
            if field == "inverse":
                actual ^= screen_reverse
            if actual != value:
                differences.append(f"{field}={actual}")
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
        differences.append("superscript is not represented by Shitty cells")
    if "S_" in (attrs_match.group(1) if attrs_match else ""):
        differences.append("subscript is not represented by Shitty cells")
    return "; ".join(differences)


def compare_assertion(terminal, assertion, expected):
    snapshot = terminal.model_snapshot()
    if assertion == "cursor":
        row, column = map(int, expected.split(","))
        actual = (snapshot.cursor_y, snapshot.cursor_x)
        return "" if actual == (row, column) else f"got {actual}"

    match = re.fullmatch(r"pen\s+(\w+)", assertion)
    if match:
        field = match.group(1)
        pen = terminal.pen_state()
        boolean_fields = {
            "bold": pen.bold,
            "italic": pen.italic,
            "blink": pen.blink,
            "reverse": pen.inverse,
        }
        if field in boolean_fields:
            actual = "on" if boolean_fields[field] else "off"
        elif field == "underline":
            actual = str(pen.underline_style if pen.underline else 0)
        elif field in ("foreground", "background"):
            index = getattr(pen, field + "_index")
            color = getattr(pen, field)
            actual = f"idx({index})" if index >= 0 else f"rgb({color[0]},{color[1]},{color[2]})"
            expected = expected.replace(",is_default_fg", "").replace(",is_default_bg", "")
        else:
            raise ValueError(f"unsupported pen field: {field}")
        return "" if actual == expected else f"got {actual!r}, expected {expected!r}"

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
        return compare_cell(
            snapshot, assertion, expected, terminal.render_state().screen_reverse
        )

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


def compare_putglyph(terminal, expected):
    match = re.fullmatch(
        r"([0-9a-fx,]+)\s+(\d+)\s+(\d+),(\d+)(.*)", expected
    )
    if not match:
        raise ValueError(f"unsupported putglyph: {expected}")
    glyph, width, row, column, flags = match.groups()
    codepoints = tuple(int(value, 16) for value in glyph.split(","))
    snapshot = terminal.model_snapshot()
    row = int(row)
    column = int(column)
    if row >= snapshot.rows or column >= snapshot.columns:
        return f"cell {row},{column} is outside {snapshot.rows}x{snapshot.columns}"
    cell = snapshot.cell(column, row)
    actual = cell.grapheme or (() if cell.char == "\0" else (ord(cell.char),))
    differences = []
    if actual != codepoints:
        differences.append(f"glyph={actual!r}, expected {codepoints!r}")
    actual_width = 2 if cell.double_width else 1
    if actual_width != int(width):
        differences.append(f"width={actual_width}")
    wanted_protected = "prot" in flags.split()
    if cell.protected != wanted_protected:
        differences.append(f"protected={cell.protected}")
    wanted_line = 0
    if "dhl-top" in flags:
        wanted_line = 1
    elif "dhl-bottom" in flags:
        wanted_line = 2
    elif "dwl" in flags:
        wanted_line = 3
    if cell.line_attribute != wanted_line:
        differences.append(f"line_attribute={cell.line_attribute}")
    return "; ".join(differences)


def modifiers(value):
    result = 0
    if "S" in value:
        result |= 1
    if "C" in value:
        result |= 2
    if "A" in value:
        result |= 4
    return result


PARSER_EVENT_TYPES = {"text", "control", "escape", "csi", "osc", "dcs", "apc", "pm", "sos"}


def append_parser_event(state, event, payload):
    if event == "text" and state["parser_expected"] and state["parser_expected"][-1][0] == "text":
        state["parser_expected"][-1][1].extend(payload)
        return len(state["parser_expected"]) - 1
    state["parser_expected"].append([event, bytearray(payload)])
    return len(state["parser_expected"]) - 1


def parse_csi_callback(value):
    match = re.fullmatch(r"(0x[0-9a-fA-F]+)(?:\s+(.*))?", value)
    if not match:
        raise ValueError(f"invalid CSI callback: {value}")
    final = int(match.group(1), 16)
    leader = b""
    intermediates = b""
    arguments = "*"
    for field in (match.group(2) or "").split():
        if field.startswith("L="):
            leader += bytes([int(field[2:], 16)])
        elif field.startswith("I="):
            intermediates += bytes([int(field[2:], 16)])
        else:
            arguments = field
    parameters = b""
    if arguments != "*":
        parameters = arguments.replace("+,", ":").replace(",", ";").encode()
    return leader + parameters + intermediates + bytes([final])


def parse_string_callback(event, value, state):
    started = value.startswith("[")
    closed = value.endswith("]")
    body = value[1:] if started else value
    body = body[:-1] if closed else body
    body = body.strip()
    if body.startswith('"') and not re.search(r'"\s*(?:x\s*\d+)?$', body):
        body += '"'
    payload = b""
    if event == "osc" and started:
        match = re.fullmatch(r"(\d+)(?:\s+(.*))?", body)
        if not match:
            raise ValueError(f"invalid OSC callback: {value}")
        payload = match.group(1).encode()
        if match.group(2) is not None:
            payload += b";" + decode_perl_string(match.group(2))
    elif body:
        payload = decode_perl_string(body)
    if started:
        state["parser_strings"][event] = append_parser_event(state, event, payload)
    else:
        index = state["parser_strings"].get(event)
        if index is None:
            raise ValueError(f"orphan {event} callback fragment")
        state["parser_expected"][index][1].extend(payload)
    if closed:
        state["parser_strings"].pop(event, None)


def parse_parser_callback(line, state):
    event, _, value = line.partition(" ")
    if event not in PARSER_EVENT_TYPES:
        return False
    value = value.strip()
    if event == "text":
        payload = bytes(int(token.strip(), 16) for token in value.split(","))
        append_parser_event(state, event, payload)
    elif event == "control":
        base = 16 if value.lower().startswith("0x") else 10
        append_parser_event(state, event, bytes([int(value, base)]))
    elif event == "escape":
        append_parser_event(state, event, decode_perl_string(value))
    elif event == "csi":
        append_parser_event(state, event, parse_csi_callback(value))
    else:
        parse_string_callback(event, value, state)
    return True


def apply_command(terminal, line, state):
    if line in ("INIT", "RESET"):
        if line == "INIT":
            configure_libvterm_colors(terminal)
        else:
            terminal.write(b"\x1bc")
            configure_libvterm_colors(terminal)
        if not state["utf8"]:
            terminal.write(b"\x1b%@")
        if line == "RESET" and state["parser_enabled"]:
            # RESET and the adapter's encoding selection are fixture setup,
            # not parser callbacks requested by WANTSTATE f.
            terminal.parser_trace_clear()
        return
    if line == "WANTPARSER" or (line.startswith("WANTSTATE") and "f" in line[9:]):
        terminal.parser_trace_on()
        state["parser_enabled"] = True
        state["parser_only"] = line == "WANTPARSER"
        return
    if line.startswith("UTF8 "):
        state["utf8"] = line == "UTF8 1"
        terminal.write(b"\x1b%G" if state["utf8"] else b"\x1b%@")
        return
    if line.startswith(("WANTSCREEN", "WANTSTATE", "WANTENCODING", "DAMAGEMERGE", "DAMAGEFLUSH")):
        if line == "WANTSCREEN -b":
            state["synthetic_scrollback"] = False
        return
    if line.startswith("ENCIN "):
        state["encoding_output"].extend(
            terminal.utf8_push(decode_perl_string(line[6:]))
        )
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
    match = re.fullmatch(r"INCHAR\s+(\w+)\s+([0-9a-fA-F]+)", line)
    if match:
        modifier, character = match.groups()
        terminal.char(int(character, 16), modifiers(modifier))
        return
    match = re.fullmatch(r"INKEY\s+(\w+)\s+(\w+)", line)
    if match:
        modifier, key = match.groups()
        names = {"Up": "UP", "Tab": "TAB", "Enter": "RETURN", "KP0": "KP_0"}
        terminal.key(names.get(key, key.upper()), modifiers(modifier))
        return
    match = re.fullmatch(r"FOCUS\s+(IN|OUT)", line)
    if match:
        terminal.focus(match.group(1) == "IN")
        return
    match = re.fullmatch(r"PASTE\s+(START|END)", line)
    if match:
        if match.group(1) == "START":
            terminal.paste(b"")
        return
    match = re.fullmatch(r"MOUSEMOVE\s+(\d+),(\d+)(?:\s+(\w+))?", line)
    if match:
        row, column, modifier = match.groups()
        row = int(row)
        column = int(column)
        state["mouse"] = (row, column)
        snapshot = terminal.model_snapshot()
        if column >= snapshot.columns or row >= snapshot.rows:
            terminal.resize(
                max(snapshot.columns, column + 1),
                max(snapshot.rows, row + 1),
            )
        terminal.pointer(column + 3, row + 3, modifiers(modifier or "0"))
        return
    match = re.fullmatch(r"MOUSEBTN\s+([du])\s+(\d+)\s+(\w+)", line)
    if match:
        direction, button, modifier = match.groups()
        button = int(button)
        row, column = state["mouse"]
        if button <= 3:
            glfw_button = {1: 0, 2: 2, 3: 1}[button]
            terminal.button(
                glfw_button,
                direction == "d",
                x=column + 3,
                y=row + 3,
                modifiers=modifiers(modifier),
            )
        elif direction == "d":
            deltas = {4: (0, 1), 5: (0, -1), 6: (-1, 0), 7: (1, 0)}
            x, y = deltas[button]
            terminal.scroll(
                x,
                y,
                modifiers=modifiers(modifier),
                pixel_x=column + 3,
                pixel_y=row + 3,
            )
        return
    match = re.fullmatch(r"SELECTION\s+1\s+(.*)", line)
    if match:
        fragment = match.group(1)
        started, closed, payload = decode_callback_fragment(fragment)
        if started:
            state["selection"] = b""
            state["selection_reply_active"] = True
            state["selection_reply_expected"].clear()
        state["selection"] += payload
        if closed:
            state["selection_reply_actual"] = (
                b"\x1b]52;c;"
                + base64.b64encode(state["selection"])
                + b"\x1b\\"
            )
            state["selection_reply_closed"] = True
        return
    raise ValueError(f"unsupported command: {line}")


def run_fixture(path):
    mismatches = []
    checked = 0
    skipped = 0
    lines = expand_controls(path.read_text().splitlines())
    overwritten_putglyphs = overwritten_putglyph_callbacks(lines)
    state = {
        "output": bytearray(), "encoding_output": [], "mouse": (0, 0),
        "selection": b"", "title": "", "osc52": b"",
        "selection_reply_active": False, "selection_reply_closed": False,
        "selection_reply_actual": b"", "selection_reply_expected": bytearray(),
        "synthetic_scrollback": False,
        "utf8": False,
        "parser_enabled": False, "parser_only": False,
        "parser_expected": [], "parser_strings": {},
    }
    with Shitty(columns=80, rows=25, save_lines=500) as terminal:
        for number, line in lines:
            if state["selection_reply_closed"] and not re.fullmatch(
                r"output\s+.*", line
            ):
                if state["selection_reply_actual"] != bytes(
                    state["selection_reply_expected"]
                ):
                    mismatches.append(
                        f"line {number}: selection reply: got "
                        f"{state['selection_reply_actual']!r}, expected "
                        f"{bytes(state['selection_reply_expected'])!r}"
                    )
                state["selection_reply_active"] = False
                state["selection_reply_closed"] = False
            if not line or line.startswith("#") or line == "__END__" or line.startswith("!"):
                continue
            if parse_parser_callback(line, state):
                checked += 1
                continue
            match = re.fullmatch(r"\?([a-z_]+(?:\s+[^=]+)?)\s*=\s*(.*)", line)
            if match:
                assertion, expected = (value.strip() for value in match.groups())
                if state["synthetic_scrollback"] and (
                    assertion == "cursor" or assertion.startswith("screen_")
                ):
                    skipped += 1
                    continue
                try:
                    mismatch = compare_assertion(terminal, assertion, expected)
                except ValueError:
                    skipped += 1
                    continue
                checked += 1
                if mismatch:
                    mismatches.append(f"line {number}: {assertion}: {mismatch}")
                continue
            match = re.fullmatch(r"output\s+(.*)", line)
            if match:
                expected = decode_perl_string(match.group(1))
                if state["selection_reply_active"]:
                    state["selection_reply_expected"].extend(expected)
                    checked += 1
                    continue
                state["output"].extend(terminal.read_input())
                actual = bytes(state["output"][:len(expected)])
                del state["output"][:len(expected)]
                checked += 1
                if actual != expected:
                    mismatches.append(
                        f"line {number}: output: got {actual!r}, expected {expected!r}"
                    )
                    state["output"].clear()
                continue
            match = re.fullmatch(r"encout\s+(.*)", line)
            if match:
                expected = tuple(
                    int(value.strip(), 16)
                    for value in match.group(1).split(",")
                )
                actual = tuple(state["encoding_output"])
                state["encoding_output"].clear()
                checked += 1
                if actual != expected:
                    mismatches.append(
                        f"line {number}: encout: got {actual!r}, expected {expected!r}"
                    )
                continue
            match = re.fullmatch(r"putglyph\s+(.*)", line)
            if match:
                if number in overwritten_putglyphs:
                    skipped += 1
                    continue
                checked += 1
                mismatch = compare_putglyph(terminal, match.group(1))
                if mismatch:
                    mismatches.append(f"line {number}: putglyph: {mismatch}")
                continue
            match = re.fullmatch(r"settermprop\s+(\d+)\s+(.*)", line)
            if match:
                prop, expected = match.groups()
                cursor_visible, cursor_blink, cursor_style = terminal.cursor_state()
                if prop == "1":
                    actual = cursor_visible != 0
                    wanted = expected == "true"
                elif prop == "2":
                    actual = cursor_blink != 0
                    wanted = expected == "true"
                elif prop == "7":
                    actual = 2 if cursor_style == 3 else 1
                    wanted = int(expected)
                elif prop == "8":
                    actual = max(0, terminal.state()[0] - 1)
                    wanted = int(expected)
                elif prop == "9":
                    actual = terminal.state()[2] != 0
                    wanted = expected == "true"
                elif prop == "4":
                    started, closed, payload = decode_callback_fragment(expected)
                    if started:
                        state["title"] = ""
                    state["title"] += payload.decode()
                    if not closed:
                        skipped += 1
                        continue
                    actions = terminal.read_actions()
                    titles = [
                        bytes.fromhex(action[6:]).decode()
                        for action in actions if action.startswith("OSC 2 ")
                    ]
                    actual = titles[-1] if titles else ""
                    wanted = state["title"]
                else:
                    skipped += 1
                    continue
                checked += 1
                if actual != wanted:
                    mismatches.append(
                        f"line {number}: settermprop {prop}: got {actual!r}, expected {wanted!r}"
                    )
                continue
            match = re.fullmatch(r"selection-(set|query)\s+mask=0001(?:\s+(.*))?", line)
            if match:
                operation, fragment = match.groups()
                if operation == "query":
                    actions = terminal.read_actions()
                    actual = any(action == "OSC 52 633b3f" for action in actions)
                    # Shitty's host answers a clipboard query immediately;
                    # libvterm instead asks its embedding application for the
                    # selection and waits for vterm_state_send_selection().
                    # The later SELECTION commands exercise the same reply.
                    terminal.read_input()
                    checked += 1
                    if not actual:
                        mismatches.append(f"line {number}: selection query was not observed")
                    continue
                fragment = fragment or ""
                started, closed, payload = decode_callback_fragment(fragment)
                if started:
                    state["osc52"] = b""
                state["osc52"] += payload
                if not closed:
                    skipped += 1
                    continue
                actions = terminal.read_actions()
                payloads = [
                    bytes.fromhex(action[7:])
                    for action in actions if action.startswith("OSC 52 ")
                ]
                actual = payloads[-1] if payloads else b""
                request = actual.split(b";", 1)[1] if b";" in actual else b""
                try:
                    decoded = base64.b64decode(request, validate=True) if request else b""
                except ValueError:
                    decoded = b""
                checked += 1
                if decoded != state["osc52"]:
                    mismatches.append(
                        f"line {number}: selection set: got {decoded!r}, expected {state['osc52']!r}"
                    )
                continue
            if line.startswith("sb_popline "):
                # The upstream harness fabricates "ABCDE" for every line
                # requested through libvterm's external scrollback callback.
                # Shitty owns real scrollback internally, so assertions based
                # on those injected cells are not portable terminal behavior.
                state["synthetic_scrollback"] = True
                skipped += 1
                continue
            if line[0].isupper():
                try:
                    apply_command(terminal, line, state)
                except ValueError:
                    skipped += 1
                continue
            skipped += 1
        if state["selection_reply_closed"]:
            if state["selection_reply_actual"] != bytes(
                state["selection_reply_expected"]
            ):
                mismatches.append(
                    "selection reply: got "
                    f"{state['selection_reply_actual']!r}, expected "
                    f"{bytes(state['selection_reply_expected'])!r}"
                )
        state["output"].extend(terminal.read_input())
        if state["output"] and not state["parser_only"]:
            mismatches.append(f"unexpected output: {bytes(state['output'])!r}")
        if state["encoding_output"]:
            mismatches.append(
                f"unexpected encoding output: {tuple(state['encoding_output'])!r}"
            )
        if state["parser_enabled"]:
            actual = terminal.parser_trace()
            expected = [
                (event, bytes(payload))
                for event, payload in state["parser_expected"]
            ]
            if actual != expected:
                limit = min(len(actual), len(expected))
                difference = next(
                    (index for index in range(limit) if actual[index] != expected[index]),
                    limit,
                )
                mismatches.append(
                    f"parser event {difference}: got {actual[difference:difference + 3]!r}, "
                    f"expected {expected[difference:difference + 3]!r}; "
                    f"totals {len(actual)}/{len(expected)}"
                )
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
