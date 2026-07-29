#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.


CASE_NAMES = (
    "ascii_iso_8859",
    "column_80",
    "attributes_vt100",
    "attributes_16color",
    "attributes_256color8",
    "attributes_256color248",
    "attributes_truecolor",
    "attributes_bce",
    "attributes_osc8",
    "back_tab",
    "cursor_motion",
    "multiline_scroll",
    "scroll",
    "wrap_across_frames",
    "unicode_combine_fallback",
    "unicode_later_combining",
    "window_resize",
)


def case_names():
    return CASE_NAMES


def expect(actual, expected, subject):
    if actual != expected:
        raise AssertionError(
            f"{subject}: expected {expected!r}, got {actual!r}"
        )


def grapheme(cell):
    if cell.double_width_continuation:
        return ()
    return cell.grapheme or (ord(cell.char),)


def clear(terminal):
    terminal.write(b"\x1b[H\x1b[2J")


def ascii_iso_8859(terminal):
    ranges = (
        *(range(start, start + 16) for start in range(0x20, 0x70, 0x10)),
        range(0x70, 0x7f),
        *(range(start, start + 16) for start in range(0xa0, 0x100, 0x10)),
    )
    lines = tuple(
        "".join(f"{codepoint:02x} {chr(codepoint)} " for codepoint in values)
        for values in ranges
    )
    terminal.write(
        b"\x1b[H\x1b[2J"
        + "\r\n".join(lines).encode("utf-8")
        + b"\r\n"
    )
    snapshot = terminal.model_snapshot()
    for row, line in enumerate(lines):
        expect(snapshot.lines[row], line.ljust(80), f"row {row + 1}")
        for item, codepoint in enumerate(ranges[row]):
            cell = snapshot.cell(item * 5 + 3, row)
            expect(grapheme(cell), (codepoint,), f"U+{codepoint:04X}")


def column_80(terminal):
    clear(terminal)
    terminal.write(b"E" * 80)
    snapshot = terminal.model_snapshot()
    expect(snapshot.lines[0], "E" * 80, "full first row")
    expect(snapshot.lines[1], " " * 80, "no eager wrap")
    expect((snapshot.cursor_x, snapshot.cursor_y), (79, 0), "pending cursor")

    terminal.write(b"\r\n" + (b"E" * 80 + b"\r\n") * 24)
    snapshot = terminal.model_snapshot()
    expect(snapshot.lines[:23], ["E" * 80] * 23, "visible full rows")
    expect(snapshot.lines[23], " " * 80, "bottom row")
    expect((snapshot.cursor_x, snapshot.cursor_y), (0, 23), "final cursor")


def attributes_vt100(terminal):
    clear(terminal)
    for value in (0, 1, 4, 5, 7):
        terminal.write(f"\x1b[{value}mE\x1b[m ".encode())
    snapshot = terminal.model_snapshot()
    cells = [snapshot.cell(column, 0) for column in range(0, 10, 2)]
    expect(
        [
            (cell.bold, cell.underline_style, cell.blink, cell.inverse)
            for cell in cells
        ],
        [
            (False, 0, False, False),
            (True, 0, False, False),
            (False, 1, False, False),
            (False, 0, True, False),
            (False, 0, False, True),
        ],
        "VT100 attributes",
    )


def attributes_16color(terminal):
    clear(terminal)
    values = (*range(30, 38), 39, *range(40, 48), 49)
    for value in values:
        terminal.write(f"\x1b[{value}mE\x1b[m ".encode())
    snapshot = terminal.model_snapshot()
    for index, value in enumerate(values):
        cell = snapshot.cell(index * 2, 0)
        if 30 <= value <= 37:
            expect(cell.foreground_index, value - 30, f"SGR {value} fg")
            expect(cell.background_index, -2, f"SGR {value} bg")
        elif value == 39:
            expect(cell.foreground_index, -2, "SGR 39 fg")
        elif 40 <= value <= 47:
            expect(cell.background_index, value - 40, f"SGR {value} bg")
            expect(cell.foreground_index, -2, f"SGR {value} fg")
        else:
            expect(cell.background_index, -2, "SGR 49 bg")


def indexed_color_case(terminal, first, last):
    clear(terminal)
    for value in range(first, last + 1):
        terminal.write(
            f"\x1b[38;5;{value}mE\x1b[m "
            f"\x1b[48;5;{value}mM\x1b[m ".encode()
        )
    snapshot = terminal.model_snapshot()
    for ordinal, value in enumerate(range(first, last + 1)):
        offset = ordinal * 4
        fg = snapshot.cells[offset]
        bg = snapshot.cells[offset + 2]
        expect(fg.char, "E", f"indexed {value} foreground glyph")
        expect(fg.foreground_index, value, f"indexed {value} foreground")
        expect(bg.char, "M", f"indexed {value} background glyph")
        expect(bg.background_index, value, f"indexed {value} background")


def attributes_256color8(terminal):
    indexed_color_case(terminal, 0, 7)


def attributes_256color248(terminal):
    indexed_color_case(terminal, 8, 255)


def truecolor(value):
    red = 255 - value * 255 // 76
    green = value * 510 // 76
    if green > 255:
        green = 510 - green
    blue = value * 255 // 76
    return red, green, blue


def attributes_truecolor(terminal):
    styles = (
        ("Normal:", (), (False, False, 0, False, False, False)),
        ("Bold:", (1,), (True, False, 0, False, False, False)),
        ("Italic:", (3,), (False, True, 0, False, False, False)),
        ("Underline:", (4,), (False, False, 1, False, False, False)),
        ("Blink:", (5,), (False, False, 0, True, False, False)),
        ("Inverse:", (7,), (False, False, 0, False, True, False)),
        ("Invisible:", (8,), (False, False, 0, False, False, True)),
        (
            "Bold, italic and underline:",
            (1, 3, 4),
            (True, True, 1, False, False, False),
        ),
    )
    clear(terminal)
    for label, parameters, _ in styles:
        terminal.write(label.encode() + b"\r\n")
        prefix = "\x1b[0" + "".join(f";{value}" for value in parameters) + "m"
        payload = bytearray()
        for value in range(77):
            background = truecolor(value)
            foreground = tuple(255 - channel for channel in background)
            payload.extend(prefix.encode())
            payload.extend(
                (
                    f"\x1b[48;2;{background[0]};{background[1]};"
                    f"{background[2]}m"
                    f"\x1b[38;2;{foreground[0]};{foreground[1]};"
                    f"{foreground[2]}mE\x1b[m"
                ).encode()
            )
        terminal.write(bytes(payload) + b"\r\n")

    snapshot = terminal.model_snapshot()
    for style_index, (_, _, flags) in enumerate(styles):
        row = style_index * 2 + 1
        for value in range(77):
            cell = snapshot.cell(value, row)
            background = truecolor(value)
            foreground = tuple(255 - channel for channel in background)
            expect(cell.char, "E", f"truecolor row {row} cell {value}")
            expect(cell.background_index, -1, "truecolor background kind")
            expect(cell.foreground_index, -1, "truecolor foreground kind")
            expect(cell.background, background, "truecolor background")
            expect(cell.foreground, foreground, "truecolor foreground")
            actual_flags = (
                cell.bold,
                cell.italic,
                cell.underline_style,
                cell.blink,
                cell.inverse,
                cell.conceal,
            )
            expect(actual_flags, flags, f"truecolor style row {row}")


def attributes_bce(terminal):
    terminal.write(
        b"\x1b[48;2;255;0;255m\x1b[H\x1b[2JTrue color\r\n"
        b"\x1b[48;5;32m\x1b[J256 color\r\n"
        b"\x1b[42m\x1b[J16 color\r\n"
        b"\x1b[0mdone\r\n"
    )
    snapshot = terminal.model_snapshot()
    for column in (0, 20, 79):
        cell = snapshot.cell(column, 0)
        expect(cell.background_index, -1, f"BCE truecolor {column}")
        expect(cell.background, (255, 0, 255), f"BCE RGB {column}")
        expect(
            snapshot.cell(column, 1).background_index,
            32,
            f"BCE indexed {column}",
        )
        expect(
            snapshot.cell(column, 2).background_index,
            2,
            f"BCE basic {column}",
        )
    expect(snapshot.lines[0][:10], "True color", "BCE row 1")
    expect(snapshot.lines[1][:9], "256 color", "BCE row 2")
    expect(snapshot.lines[2][:8], "16 color", "BCE row 3")
    expect(snapshot.lines[3][:4], "done", "BCE row 4")
    for column in range(4):
        expect(
            snapshot.cell(column, 3).background_index,
            -2,
            f"reset background {column}",
        )
    expect(snapshot.cell(4, 3).background_index, 2, "erased BCE survives reset")


def osc8(parameters, uri, text):
    return (
        b"\x1b]8;"
        + parameters
        + b";"
        + uri
        + b"\x1b\\"
        + text
        + b"\x1b]8;;\x1b\\"
    )


def attributes_osc8(terminal):
    lines = (
        (b"", b"http://example.com", b"This is a link"),
        (b"id=foo", b"http://example.com", b"link"),
        (
            b"foo=bar:bar=baz",
            b"http://example.com",
            b"link",
        ),
        (
            b"",
            b"vscode://file/home/achin/test:1:1",
            b"test",
        ),
    )
    clear(terminal)
    for parameters, uri, text in lines:
        terminal.write(osc8(parameters, uri, text) + b"\r\n")
    snapshot = terminal.model_snapshot()
    for row, (_, uri, text) in enumerate(lines):
        expect(snapshot.lines[row][:len(text)], text.decode(), f"OSC 8 row {row}")
        handles = {
            snapshot.cell(column, row).hyperlink
            for column in range(len(text))
        }
        if len(handles) != 1 or 0 in handles:
            raise AssertionError(f"OSC 8 row {row}: invalid handles {handles!r}")
        for column in range(len(text)):
            expect(
                terminal.hyperlink_bytes(column, row),
                uri,
                f"OSC 8 URI row {row} column {column}",
            )
        expect(snapshot.cell(len(text), row).hyperlink, 0, f"OSC 8 close row {row}")


def back_tab(terminal):
    terminal.write(
        b"hello, wurld\x1b[Zo\r\n"
        b"hello, wurld\x1b[2Zo\r\n"
        b"hello, wurld\x1b[99Z9\r\n"
        b"hello, wurld\x1b[It\r\n"
        b"\x1b[99I#\r\n"
    )
    snapshot = terminal.model_snapshot()
    expect(snapshot.lines[0], "hello, world".ljust(80), "CBT once")
    expect(snapshot.lines[1], "oello, wurld".ljust(80), "CBT twice")
    expect(snapshot.lines[2], "9ello, wurld".ljust(80), "CBT clamp")
    expect(snapshot.lines[3], "hello, wurld    t".ljust(80), "CHT")
    expect(snapshot.lines[4], " " * 79 + "#", "CHT clamp")


def cursor_motion(terminal):
    points = (
        (1, 1, "A"),
        (10, 1, "B"),
        (1, 2, "C"),
        (1, 4, "D"),
        (10, 4, "E"),
        (1, 7, "F"),
        (1, 11, "G"),
        (10, 11, "H"),
        (1, 16, "I"),
        (2, 16, "J"),
        (1, 22, "K"),
        (60, 23, "L"),
        (59, 23, "M"),
        (57, 23, "N"),
        (54, 23, "O"),
        (50, 23, "P"),
        (45, 23, "Q"),
        (39, 23, "R"),
        (32, 23, "S"),
        (1, 24, "done"),
    )
    clear(terminal)
    for column, row, text in points:
        terminal.write(f"\x1b[{row};{column}H{text}".encode())
    snapshot = terminal.model_snapshot()
    for column, row, text in points:
        expect(
            snapshot.lines[row - 1][column - 1:column - 1 + len(text)],
            text,
            f"CUP {row};{column}",
        )
    expect((snapshot.cursor_x, snapshot.cursor_y), (4, 23), "motion cursor")


def multiline_scroll(terminal):
    clear(terminal)
    blank = " " * 80
    expected = [blank] * 24
    for final in ("L", "M"):
        for count in (0, 1, 2, 22, 23, 24, 25, 26):
            terminal.write(f"{count}\r\x1b[{count}{final}".encode())
            text = str(count)
            expected[0] = text + expected[0][len(text):]
            affected = min(max(count, 1), 24)
            if final == "L":
                expected = [blank] * affected + expected[:24 - affected]
            else:
                expected = expected[affected:] + [blank] * affected
            snapshot = terminal.model_snapshot()
            expect(snapshot.lines, expected, f"CSI {count}{final}")
            expect((snapshot.cursor_x, snapshot.cursor_y), (0, 0), "IL/DL cursor")


def scroll(terminal):
    clear(terminal)
    for line in range(1, 25):
        terminal.write(f"\r\ntext {line}".encode())
    terminal.write(
        b"\x1b[4S"
        b"\x1b[2T"
        b"\rBad line"
        b"\x1b[24;1HLast line"
        b"\x1b[HFirst line\r\n"
    )
    snapshot = terminal.model_snapshot()
    stripped = [line.rstrip() for line in snapshot.lines]
    expect(stripped[0], "First line", "scroll first line")
    expect(stripped[1], "", "scroll second line")
    expect(stripped[2:22], [f"text {line}" for line in range(5, 25)], "scroll body")
    expect(stripped[22], "", "scroll penultimate line")
    expect(stripped[23], "Last line", "scroll last line")
    expect((snapshot.cursor_x, snapshot.cursor_y), (0, 1), "scroll cursor")


def wrap_across_frames(terminal):
    upper = b"abcd" + b"X" * 72 + b"1234"
    lower = b"ABCD" + b"x" * 72 + b"5678"
    clear(terminal)
    for _ in range(10):
        terminal.write(upper)
        terminal.write(lower)
    terminal.write(b"\r\n")
    snapshot = terminal.model_snapshot()
    expect(
        snapshot.lines[:20],
        [value.decode() for _ in range(10) for value in (upper, lower)],
        "wrapped frame rows",
    )
    expect(snapshot.lines[20:], [" " * 80] * 4, "wrapped frame tail")
    expect(
        [snapshot.cell(79, row).wrapped for row in range(20)],
        [True] * 19 + [False],
        "wrapped row flags",
    )
    expect((snapshot.cursor_x, snapshot.cursor_y), (0, 20), "wrapped cursor")


def unicode_combine_fallback(terminal):
    terminal.write(b"0\x1b[1J\xcc\xb4")
    snapshot = terminal.model_snapshot()
    expect(grapheme(snapshot.cell(1, 0)), (0x334,), "orphan U+0334")
    expect((snapshot.cursor_x, snapshot.cursor_y), (2, 0), "orphan cursor")


def unicode_later_combining(terminal):
    terminal.write(b"abc\r\n\xcc\x82\r\ndef\r\n")
    snapshot = terminal.model_snapshot()
    expect(snapshot.lines[0][:3], "abc", "combining first row")
    expect(grapheme(snapshot.cell(0, 1)), (0x302,), "orphan U+0302")
    expect(snapshot.lines[2][:3], "def", "combining final row")
    terminal.select_start(0, 1)
    terminal.select_update(1, 1)
    expect(terminal.select_finish(), b"\xcc\x82", "orphan selection")


def draw_resize_frame(terminal, columns, rows):
    terminal.write(
        (
            f"\x1b[2J\x1b[H{columns}x{rows}"
            f"\x1b[{rows};{columns}H#"
        ).encode()
    )
    snapshot = terminal.model_snapshot()
    expect((snapshot.columns, snapshot.rows), (columns, rows), "resize geometry")
    expect(snapshot.lines[0][:len(f"{columns}x{rows}")], f"{columns}x{rows}", "resize title")
    expect(snapshot.cell(columns - 1, rows - 1).char, "#", "resize corner")


def window_resize(terminal):
    terminal.write(b"\x1b[?1049h")
    draw_resize_frame(terminal, 80, 24)
    terminal.resize(60, 14)
    draw_resize_frame(terminal, 60, 14)
    terminal.resize(100, 30)
    draw_resize_frame(terminal, 100, 30)
    terminal.resize(80, 24)
    draw_resize_frame(terminal, 80, 24)
    expect(terminal.conformance_state()["screen"], "Alternate", "resize screen")


CASES = {
    name: globals()[name]
    for name in CASE_NAMES
}


def run_case(name, terminal):
    try:
        case = CASES[name]
    except KeyError as error:
        raise KeyError(f"unknown Mosh semantic case: {name}") from error
    case(terminal)
