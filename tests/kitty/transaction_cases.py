# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

from harness import Shitty


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def logical_line(snapshot, row):
    first = row * snapshot.columns
    cells = snapshot.cells[first:first + snapshot.columns]
    return "".join(
        cell.char for cell in cells
        if not cell.double_width_continuation
    ).rstrip(" ")


def traced(terminal, payload):
    terminal.parser_trace_clear()
    terminal.write(payload)
    return terminal.parser_trace()


def charsets():
    with Shitty(columns=5, rows=5, save_lines=0) as terminal:
        terminal.parser_trace_on()
        terminal.write(b"\xc3")
        require(logical_line(terminal.model_snapshot(), 0) == "",
                "incomplete UTF-8 was rendered")
        terminal.write(b"\xa1")
        require(logical_line(terminal.model_snapshot(), 0) == "á",
                "split UTF-8 was not rendered")

    for payload, expected_state in (
        (b"\x1b)0\x0e/_", (0, 1, 0, 0)),
        (b"\x1b(0/_", (1, 0, 0, 0)),
    ):
        with Shitty(columns=5, rows=5, save_lines=0) as terminal:
            terminal.write(payload)
            # VT525 and the majority of current implementations map the
            # DEC-special '_' blank to U+0020. Kitty uses U+00A0.
            require(logical_line(terminal.model_snapshot(), 0) == "/",
                    "DEC-special blank differs")
            require(terminal.charset_state() == expected_state,
                    "charset designation or shift differs")


def parser_threading():
    scenarios = (
        (
            b"a\x1b]2;some title",
            b" full\x1b\\",
            [("text", b"a"), ("osc", b"2;some title full")],
            ["OSC 2 736f6d65207469746c652066756c6c"],
        ),
        (
            b"a\x1b]",
            b"2;title\x1b\\",
            [("text", b"a"), ("osc", b"2;title")],
            ["OSC 2 7469746c65"],
        ),
        (
            b"a\x1b",
            b"]2;title\x1b\\",
            [("text", b"a"), ("osc", b"2;title")],
            ["OSC 2 7469746c65"],
        ),
        (
            b"a\x1b]2;some title\x1b",
            b"\\b",
            [
                ("text", b"a"),
                ("osc", b"2;some title"),
                ("text", b"b"),
            ],
            ["OSC 2 736f6d65207469746c65"],
        ),
        (
            b"1\x1b",
            b"E2",
            [("text", b"1"), ("escape", b"E"), ("text", b"2")],
            [],
        ),
        (
            b"1\x1b[2",
            b"3mx",
            [("text", b"1"), ("csi", b"23m"), ("text", b"x")],
            [],
        ),
        (
            b"1\x1b",
            b"[23mx",
            [("text", b"1"), ("csi", b"23m"), ("text", b"x")],
            [],
        ),
        (
            b"1\x1b[",
            b"23mx",
            [("text", b"1"), ("csi", b"23m"), ("text", b"x")],
            [],
        ),
    )
    for first, second, expected_trace, expected_actions in scenarios:
        with Shitty(columns=8, rows=5, save_lines=0) as terminal:
            terminal.parser_trace_on()
            terminal.write(first)
            require(not terminal.read_actions(),
                    "incomplete sequence produced a host action")
            terminal.write(second)
            require(terminal.parser_trace() == expected_trace,
                    f"split parser trace differs for {first!r}")
            require(terminal.read_actions() == expected_actions,
                    f"split host action differs for {first!r}")


def simple_parsing():
    with Shitty(columns=5, rows=5, save_lines=0) as terminal:
        terminal.write(b"12")
        require(logical_line(terminal.model_snapshot(), 0) == "12",
                "initial text differs")
        terminal.write(b"3456")
        snapshot = terminal.model_snapshot()
        require(logical_line(snapshot, 0) == "12345", "wrap row differs")
        require(logical_line(snapshot, 1) == "6", "wrapped glyph differs")

        terminal.write(b"\n123\n\r45")
        snapshot = terminal.model_snapshot()
        require(logical_line(snapshot, 1) == "6", "LF changed old row")
        require(logical_line(snapshot, 2) == " 123", "LF column differs")
        require(logical_line(snapshot, 3) == "45", "CR position differs")

        terminal.write(b"\rabcde")
        require(logical_line(terminal.model_snapshot(), 3) == "abcde",
                "ASCII overwrite differs")
        terminal.write("\rßxyz1".encode())
        require(logical_line(terminal.model_snapshot(), 3) == "ßxyz1",
                "UTF-8 overwrite differs")
        terminal.write("ニチ ".encode())
        require(logical_line(terminal.model_snapshot(), 4) == "ニチ",
                "wide text differs")

    controls = (
        "\x84\x85\x88\x8d\x8e\x8f\x90\x96"
        "\x97\x98\x9a\x9b\x9c\x9d\x9e\x9f"
    )
    with Shitty(columns=20, rows=5, save_lines=0) as terminal:
        terminal.write(controls.encode())
        require(logical_line(terminal.model_snapshot(), 0) == controls,
                "UTF-8 encoded C1 codepoints differ")

    with Shitty(columns=8, rows=5, save_lines=0) as terminal:
        terminal.write(b"\xf0\x9f\x98")
        terminal.write(b"\x1b\x1b%a")
        # ECMA-48 ignores the unknown ESC % a sequence. Kitty additionally
        # prints its bytes, but current VTE, Foot and xterm-style parsers do
        # not turn an unknown control sequence into visible text.
        require(logical_line(terminal.model_snapshot(), 0) == "\ufffd",
                "interrupted UTF-8 maximal subpart was not replaced")


def esc_codes():
    with Shitty(columns=5, rows=5, save_lines=0) as terminal:
        terminal.parser_trace_on()
        require(
            traced(terminal, b"12\x1bDa")
            == [("text", b"12"), ("escape", b"D"), ("text", b"a")],
            "IND trace differs",
        )
        snapshot = terminal.model_snapshot()
        require(logical_line(snapshot, 0) == "12", "IND source differs")
        require(logical_line(snapshot, 1) == "  a", "IND target differs")

        require(
            traced(terminal, b"\x1bxa")
            == [("escape", b"x"), ("text", b"a")],
            "unknown ESC recovery differs",
        )
        require(
            traced(terminal, b"\x1bc123")
            == [("escape", b"c"), ("text", b"123")],
            "RIS trace differs",
        )
        require(logical_line(terminal.model_snapshot(), 0) == "123",
                "RIS did not reset the screen")

        # A new ESC cancels the incomplete charset designation; the following
        # unknown ESC a is ignored rather than rendered as plain text.
        require(
            traced(terminal, b"\x1b.\x1ba") == [("escape", b"a")],
            "charset cancellation differs",
        )


def csi_code_rep():
    with Shitty(columns=8, rows=5, save_lines=0) as terminal:
        terminal.write(b"\x1b[1b")
        require(logical_line(terminal.model_snapshot(), 0) == "",
                "REP without a preceding graphic changed the screen")
        terminal.write(b"x\x1b[7b")
        require(logical_line(terminal.model_snapshot(), 0) == "xxxxxxxx",
                "REP count differs")
        terminal.write(b"\x1b[1;3H")
        terminal.write(b"\x1b[byz\x1b[b")
        require(logical_line(terminal.model_snapshot(), 0) == "xxxyzzxx",
                "REP transaction differs")

    with Shitty(columns=8, rows=5, save_lines=0) as terminal:
        terminal.write(b" \x1b[3b")
        require(all(
            cell.drawn for cell in terminal.model_snapshot().cells[:4]
        ), "REP did not repeat an explicit space")

    with Shitty(columns=8, rows=5, save_lines=0) as terminal:
        terminal.write(b"\t\x1b[b")
        snapshot = terminal.model_snapshot()
        require(snapshot.cursor_x == 7, "HT cursor position differs")
        require(not any(cell.drawn for cell in snapshot.cells[:8]),
                "REP repeated HT as a graphic cell")


def oth_codes():
    for kind, introducer in (
        ("apc", b"_"),
        ("pm", b"^"),
        ("sos", b"X"),
    ):
        with Shitty(columns=5, rows=5, save_lines=0) as terminal:
            terminal.parser_trace_on()
            payload = b"a\x1b" + introducer + b"+\\+\x1b\\bcde"
            require(
                traced(terminal, payload)
                == [
                    ("text", b"a"),
                    (kind, b"+\\+"),
                    ("text", b"bcde"),
                ],
                f"{kind.upper()} trace differs",
            )
            require(logical_line(terminal.model_snapshot(), 0) == "abcde",
                    f"{kind.upper()} recovery differs")


CASES = {
    "charsets": charsets,
    "parser_threading": parser_threading,
    "simple_parsing": simple_parsing,
    "esc_codes": esc_codes,
    "csi_code_rep": csi_code_rep,
    "oth_codes": oth_codes,
}
