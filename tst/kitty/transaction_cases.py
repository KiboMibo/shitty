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
        terminal.write(b"\x1bc")
        snapshot = terminal.model_snapshot()
        require(all(
            logical_line(snapshot, row) == "" for row in (1, 2, 3)
        ), "RIS did not clear the intermediate rows")

    controls = (
        "\x84\x85\x88\x8d\x8e\x8f\x90\x96"
        "\x97\x98\x9a\x9b\x9c\x9d\x9e\x9f"
    )
    with Shitty(columns=20, rows=5, save_lines=0) as terminal:
        terminal.write(controls.encode())
        snapshot = terminal.model_snapshot()
        require(logical_line(snapshot, 0) == controls,
                "UTF-8 encoded C1 codepoints differ")
        require(all(
            logical_line(snapshot, row) == "" for row in (1, 2, 3)
        ), "UTF-8 encoded C1 codepoints changed lower rows")

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


def csi_codes():
    with Shitty(columns=5, rows=5, save_lines=0) as terminal:
        terminal.parser_trace_on()
        terminal.write(b"abcde\x1b[1;1H")
        require(
            traced(terminal, b"x\x1b[2@y")
            == [("text", b"x"), ("csi", b"2@"), ("text", b"y")],
            "ICH trace differs",
        )
        require(logical_line(terminal.model_snapshot(), 0) == "xy bc",
                "ICH screen mutation differs")

        for payload in (
            b"x\x1b[2;-7@y",
            b"x\x1b[-0001234567890@y",
            b"x\x1b[2-3@y",
        ):
            require(
                traced(terminal, payload) == [("text", b"xy")],
                f"invalid CSI was not discarded for {payload!r}",
            )

        for payload, expected in (
            (b"x\x1b[2;7@y", b"2;7@"),
            (b"x\x1b[@y", b"@"),
            (b"x\x1b[345@y", b"345@"),
            (b"x\x1b[345;@y", b"345;0@"),
        ):
            trace = traced(terminal, payload)
            require(trace == [
                ("text", b"x"),
                ("csi", expected),
                ("text", b"y"),
            ], f"ICH parameter trace differs for {payload!r}")

    with Shitty(columns=5, rows=5, save_lines=0) as terminal:
        for payload, expected in (
            (b"\x1b[H", (0, 0)),
            (b"\x1b[4H", (0, 3)),
            (b"\x1b[4;0H", (0, 3)),
            (b"\x1b[3;2H", (1, 2)),
            (b"\x1b[3;2;H", (1, 2)),
            (b"\x1b[00000000003;0000000000000002H", (1, 2)),
            (b"\x1b[0001234567890H", (0, 4)),
        ):
            terminal.write(payload)
            snapshot = terminal.model_snapshot()
            require((snapshot.cursor_x, snapshot.cursor_y) == expected,
                    f"CUP differs for {payload!r}")

        terminal.write(b"abcde\x1b[?2J")
        require(not any(
            cell.drawn for cell in terminal.model_snapshot().cells
        ), "DECSED did not erase the display")

        terminal.write(b"\x1b[20;4h")
        state = terminal.conformance_state()
        require(state["LNM"] and state["IRM"], "SM did not set LNM/IRM")
        terminal.write(b"\x1b[20;4;20l")
        state = terminal.conformance_state()
        require(not state["LNM"] and not state["IRM"],
                "RM did not reset LNM/IRM")

        terminal.write(b"\x1b[?1000;1004h")
        mouse_mode, _, focus_events, _ = terminal.state()
        require(mouse_mode != 0 and focus_events,
                "private SM did not set mouse/focus modes")

        terminal.write(b"\x1b[=c")
        require(
            terminal.read_input() == b"\x1bP!|00000000\x1b\\",
            "tertiary DA reply differs",
        )

    # This block asserts bold-brightened pen indices; the option is off
    # by default now (issue 59).
    with Shitty(
        columns=5, rows=5, save_lines=0, extra_arguments=("-boldColors",)
    ) as terminal:
        terminal.write(b"\x1b[1;2;3;4;7;9;34;44m")
        pen = terminal.pen_state()
        require(
            pen.bold and pen.faint and pen.italic and pen.underline
            and pen.inverse and pen.strike,
            "compound SGR attributes differ",
        )
        require(pen.underline_style == 1, "single underline differs")
        require(
            pen.foreground_index == 12 and pen.background_index == 4,
            "indexed SGR colors differ",
        )

        terminal.write(b"\x1b[38;5;1;48;5;7m")
        pen = terminal.pen_state()
        require(
            pen.foreground_index == 1 and pen.background_index == 7,
            "256-color SGR differs",
        )

        terminal.write(b"\x1b[38;2;1;2;3;48;2;7;8;9m")
        pen = terminal.pen_state()
        require(
            pen.foreground == (1, 2, 3)
            and pen.background == (7, 8, 9),
            "direct-color SGR differs",
        )

        terminal.write(b"\x1b[0;2m")
        pen = terminal.pen_state()
        require(pen.faint and not pen.bold, "SGR reset-plus-faint differs")
        terminal.write(b"\x1b[;2m")
        pen = terminal.pen_state()
        require(pen.faint and not pen.bold, "empty SGR parameter differs")
        terminal.write(b"\x1b[m")
        pen = terminal.pen_state()
        require(not pen.faint and not pen.bold, "empty SGR did not reset")
        terminal.write(b"\x1b[1;;2m")
        pen = terminal.pen_state()
        require(pen.faint and not pen.bold, "middle empty SGR differs")

        terminal.write(b"\x1b[38:2:1:2:3;48:5:9;58;5;7;4:5mX")
        pen = terminal.pen_state()
        cell = terminal.model_snapshot().cell(0, 0)
        require(
            pen.foreground == (1, 2, 3)
            and pen.background_index == 9
            and pen.underline_style == 5,
            "colon-form SGR differs",
        )
        require(cell.underline_index == 7,
                "underline color was not applied to the cell")

    with Shitty(columns=5, rows=5, save_lines=0) as terminal:
        terminal.write(b"\x1b[5n\x1b[6n")
        require(
            terminal.read_input() == b"\x1b[0n\x1b[1;1R",
            "DSR reply differs",
        )

        terminal.write(b"12345\x1b[6n")
        # DEC autowrap remains pending at the last column until another
        # printable character arrives. Kitty advances immediately instead.
        require(
            terminal.read_input() == b"\x1b[1;5R",
            "CPR pending-wrap position differs",
        )

        terminal.write(b"\x1b[?1h\x1b[?1$p")
        require(
            terminal.read_input() == b"\x1b[?1;1$y",
            "DECRQM set-state report differs",
        )
        terminal.write(b"\x1b[?1l\x1b[?1$p")
        require(
            terminal.read_input() == b"\x1b[?1;2$y",
            "DECRQM reset-state report differs",
        )

        terminal.write(b"\x1b[2;4r\x1bP$qr\x1b\\")
        require(
            terminal.read_input() == b"\x1bP1$r2;4r\x1b\\",
            "DECSTBM or its DECRQSS report differs",
        )
        terminal.write(b"\x1b[r\x1bP$qr\x1b\\")
        require(
            terminal.read_input() == b"\x1bP1$r1;5r\x1b\\",
            "default DECSTBM margins differ",
        )

        terminal.write(b"\x1b[1 q")
        visible, blink, style = terminal.cursor_state()
        require(visible and blink and style == 1,
                "DECSCUSR blinking block differs")

        terminal.parser_trace_on()
        for payload, expected in (
            (b"\x1b[3 @", b"3 @"),
            (b"\x1b[3 A", b"3 A"),
            (b"\x1b[3;4 S", b"3;4 S"),
            (b"\x1b[1T", b"1T"),
            (b"\x1b[T", b"T"),
            (b"\x1b[+T", b"+T"),
        ):
            require(traced(terminal, payload) == [("csi", expected)],
                    f"ECMA CSI trace differs for {payload!r}")

        terminal.write(b"\x1b[?2026$p")
        require(
            terminal.read_input() == b"\x1b[?2026;2$y",
            "initial synchronized-output report differs",
        )
        terminal.write(b"\x1b[?2026h\x1b[?2026$p")
        require(
            terminal.read_input() == b"\x1b[?2026;1$y",
            "set synchronized-output report differs",
        )
        terminal.write(b"\x1b[?2026l\x1b[?2026$p")
        require(
            terminal.read_input() == b"\x1b[?2026;2$y",
            "reset synchronized-output report differs",
        )

    with Shitty(
        columns=5,
        rows=5,
        save_lines=0,
        extra_arguments=("-allowWindowOps", "true"),
    ) as terminal:
        terminal.write(b"\x1b[14t\x1b[14;2t")
        require(
            terminal.read_input() == b"\x1b[4;5;5t\x1b[4;9;9t",
            "window pixel-size reports differ",
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


def osc_codes():
    with Shitty(columns=20, rows=5, save_lines=0) as terminal:
        terminal.parser_trace_on()
        payload = b"a\x1b]2;x\\ryz\x1b\\bcde"
        require(
            traced(terminal, payload)
            == [
                ("text", b"a"),
                ("osc", b"2;x\\ryz"),
                ("text", b"bcde"),
            ],
            "OSC title transaction trace differs",
        )
        require(logical_line(terminal.model_snapshot(), 0) == "abcde",
                "OSC title transaction changed visible text")
        require(
            terminal.read_actions() == ["OSC 2 785c72797a"],
            "OSC title action differs",
        )

        # A bare OSC has no numeric selector in ECMA/xterm syntax. Kitty
        # treats it as the obsolete title-and-icon form; current terminals
        # generally ignore it.
        require(
            traced(terminal, b"\x1b]\x07") == [("osc", b"")],
            "empty OSC trace differs",
        )
        require(not terminal.read_actions(), "empty OSC was dispatched")
        require(
            traced(terminal, b"1\x1b]ab\x072")
            == [
                ("text", b"1"),
                ("osc", b"ab"),
                ("text", b"2"),
            ],
            "bare OSC transaction trace differs",
        )
        require(not terminal.read_actions(), "bare OSC was dispatched")

        terminal.write(b"\x1b]2;;;;\x07\x1b]2;\x07")
        require(
            terminal.read_actions() == [
                "OSC 2 3b3b3b",
                "OSC 2 ",
            ],
            "OSC title separator handling differs",
        )

        terminal.write(
            b"\x1b]9;\x07"
            b"\x1b]9;test it with a nice long string\x07"
        )
        require(
            terminal.read_actions() == [
                "NOTIFY   ",
                "NOTIFY   "
                "7465737420697420776974682061206e696365206c6f6e6720737472696e67",
            ],
            "OSC 9 notification transaction differs",
        )

        # Kitty's old OSC 99 callback accepted arbitrary metadata. The
        # standardized notification protocol rejects malformed known fields.
        terminal.write(b"\x1b]99;moo=foo;test it\x07")
        require(not terminal.read_actions(),
                "malformed OSC 99 notification was accepted")

        terminal.write(b"\x1b]8;;\x07")
        terminal.write(b"\x1b]8moo\x07\x1b]8;moo\x07")
        require(terminal.hyperlink_count() == 0,
                "malformed OSC 8 allocated metadata")

        terminal.write(b"\x1b]8;id=xyz;\x07")
        terminal.write(
            b"\x1b]8;moo:x=z:id=xyz:id=abc;http://yay;.com\x07"
            b"Z\x1b]8;;\x07"
        )
        require(
            terminal.hyperlink(7, 0) == "http://yay;.com",
            "OSC 8 URI or parameter parsing differs",
        )

        large = b"1" * 1024
        terminal.write(b"\x1b]52;p;" + large + b"\x07")
        terminal.write(b"\x1b]52;p;xyz\x07")
        terminal.write(b"\x1b]22;?__current__\x07")
        require(
            terminal.read_actions() == [
                "OSC 52 " + (b"p;" + large).hex(),
                "OSC 52 703b78797a",
                "OSC 22 3f5f5f63757272656e745f5f",
            ],
            "OSC clipboard/dynamic-color forwarding differs",
        )


def dcs_codes():
    with Shitty(columns=5, rows=5, save_lines=0) as terminal:
        terminal.parser_trace_on()
        require(
            traced(terminal, b"a\x1bP+q6b696e64\x1b\\bcde")
            == [
                ("text", b"a"),
                ("dcs", b"+q6b696e64"),
                ("text", b"bcde"),
            ],
            "XTGETTCAP transaction trace differs",
        )
        require(logical_line(terminal.model_snapshot(), 0) == "abcde",
                "DCS transaction changed visible text")
        # Kitty's test callback injects a fake value for the name "kind".
        # The product correctly reports the unknown capability as absent.
        require(
            terminal.read_input() == b"\x1bP0+r6b696e64\x1b\\",
            "unknown XTGETTCAP reply differs",
        )

        terminal.write(b"\x1bP+q544e;436f;524742\x1b\\")
        require(
            terminal.read_input()
            == b"\x1bP1+r544e=787465726d2d323536636f6c6f72\x1b\\"
               b"\x1bP1+r436f=323536\x1b\\"
               b"\x1bP1+r524742=38\x1b\\",
            "known XTGETTCAP replies differ",
        )

        terminal.write(b"\x1bP$q q\x1b\\")
        require(
            terminal.read_input() == b"\x1bP1$r2 q\x1b\\",
            "DECRQSS cursor-style reply differs",
        )
        terminal.write(b"\x1bP$qm\x1b\\")
        require(
            terminal.read_input() == b"\x1bP1$r0m\x1b\\",
            "DECRQSS default SGR reply differs",
        )

        terminal.write(
            b"\x1b[0;34;102;1;2;3;4m"
            b"\x1bP$qm\x1b\\"
        )
        sgr = terminal.read_input()
        require(
            sgr == b"\x1bP1$r0;1;2;3;4;34;102m\x1b\\",
            "DECRQSS indexed SGR reply differs",
        )

        terminal.write(
            b"\x1b[0;38:5:200;58:2:10:11:12m"
            b"\x1bP$qm\x1b\\"
        )
        sgr = terminal.read_input()
        require(
            sgr == b"\x1bP1$r0;38:5:200;58:2::10:11:12m\x1b\\",
            "DECRQSS extended SGR reply differs",
        )

        terminal.write(b"\x1b[2;4r\x1bP$qr\x1b\\")
        require(
            terminal.read_input() == b"\x1bP1$r2;4r\x1b\\",
            "DECRQSS margin reply differs",
        )

        for payload in (
            b"\x1bP@kitty-cmd{abc\x1b\\",
            b"\x1bP@kitty-print|YWJjZA==\x1b\\",
            b"\x1bP=1s\x1b\\",
            b"\x1bP=2s\x1b\\",
        ):
            trace = traced(terminal, payload)
            require(trace == [("dcs", payload[2:-2])],
                    f"proprietary DCS trace differs for {payload!r}")
            require(not terminal.read_actions(),
                    f"proprietary DCS was dispatched for {payload!r}")
            require(not terminal.read_input(),
                    f"proprietary DCS replied for {payload!r}")


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
    "csi_codes": csi_codes,
    "csi_code_rep": csi_code_rep,
    "osc_codes": osc_codes,
    "dcs_codes": dcs_codes,
    "oth_codes": oth_codes,
}
