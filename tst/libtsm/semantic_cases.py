#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.


CASE_NAMES = (
    "test_screen_init",
    "test_screen_null",
    "test_screen_resize_alt_colors",
    "test_screen_sb_get_line_pos",
    "test_screen_copy_incomplete",
    "test_screen_copy_one_cell",
    "test_screen_copy_line",
    "test_screen_copy_line_scrolled",
    "test_screen_copy_lines",
    "test_screen_copy_lines_scrolled",
    "test_screen_copy_line_sb",
    "test_screen_copy_line_sb_scrolled",
    "test_screen_copy_line_sb_scrolled_invalid",
    "test_screen_copy_lines_sb",
    "test_screen_copy_lines_sb_scrolled",
    "test_screen_copy_lines_sb_scrolled_cut_off",
    "test_vte_init",
    "test_vte_null",
    "test_vte_custom_palette",
    "test_vte_osc_query",
    "test_vte_osc4",
    "test_vte_backspace_key",
    "test_vte_get_flags",
    "test_vte_decrqm_no_reset",
    "test_vte_csi_cursor_up_down",
    "test_mouse_cb_x10",
    "test_mouse_x10",
    "test_mouse_cb_sgr",
    "test_mouse_sgr",
    "test_mouse_sgr_cell_change",
    "test_mouse_cb_pixels",
    "test_mouse_pixels",
)


def case_names():
    return CASE_NAMES


def expect(actual, expected, subject):
    if actual != expected:
        raise AssertionError(
            f"{subject}: expected {expected!r}, got {actual!r}"
        )


def selection(terminal, start, end):
    terminal.select_start(*start)
    terminal.select_update(*end)
    return terminal.select_finish()


def rows(*values):
    return b"".join(
        f"\x1b[{row};1H".encode() + value
        for row, value in enumerate(values, 1)
    )


def fill_history(terminal, count):
    terminal.write((b"\r\n" * count))


def test_screen_init(factory):
    with factory(columns=5, rows=5) as terminal:
        expect(
            (terminal.model_snapshot().columns, terminal.model_snapshot().rows),
            (5, 5),
            "screen construction",
        )


def test_screen_null(factory):
    with factory(columns=5, rows=5) as terminal:
        terminal.write(b"")
        terminal.resize(5, 5)
        expect(terminal.select_finish(), b"", "empty selection")
        expect(terminal.read_input(), b"", "empty output")


def test_screen_resize_alt_colors(factory):
    with factory(columns=2, rows=2) as terminal:
        terminal.write(b"\x1b[?1049h\x1b[48;2;255;0;0m\x1b[2J")
        for cell in terminal.model_snapshot().cells:
            expect(cell.background, (255, 0, 0), "alternate red background")
        terminal.resize(4, 4)
        terminal.write(b"\x1b[?1049l")
        snapshot = terminal.model_snapshot()
        expect((snapshot.columns, snapshot.rows), (4, 4), "resized primary")
        for cell in snapshot.cells:
            expect(cell.background_index, -2, "primary default background")


def test_screen_sb_get_line_pos(factory):
    with factory(columns=5, rows=5, save_lines=5) as terminal:
        expect(terminal.scrollback_state(), (0, 5, 5, 0), "empty history")
        fill_history(terminal, 5)
        expect(terminal.scrollback_state(), (1, 6, 5, 1), "one history row")
        fill_history(terminal, 2)
        expect(terminal.scrollback_state(), (3, 8, 5, 3), "three history rows")
        for expected in (2, 1, 0, 0):
            terminal.wheel_up()
            expect(terminal.snapshot().view_offset, 3 - expected, "scroll up")
        terminal.wheel_down(3)
        expect(terminal.snapshot().view_offset, 0, "scroll to live screen")


def hello_screen(factory, save_lines=10):
    terminal = factory(columns=80, rows=40, save_lines=save_lines)
    terminal.write(rows(b"", b"   Hello World!", b"Filler Text"))
    return terminal


def test_screen_copy_incomplete(factory):
    with hello_screen(factory) as terminal:
        terminal.select_start(3, 1)
        expect(terminal.select_finish(), b"", "unfinished selection")


def test_screen_copy_one_cell(factory):
    with hello_screen(factory) as terminal:
        expect(selection(terminal, (3, 1), (4, 1)), b"H", "one cell")


def test_screen_copy_line(factory):
    with hello_screen(factory) as terminal:
        checks = (
            ((3, 1), (15, 1), b"Hello World!"),
            ((3, 1), (8, 1), b"Hello"),
            ((0, 1), (8, 1), b"   Hello"),
            ((15, 1), (3, 1), b"Hello World!"),
            ((8, 1), (3, 1), b"Hello"),
        )
        for start, end, expected in checks:
            expect(selection(terminal, start, end), expected, "line selection")


def test_screen_copy_line_scrolled(factory):
    with factory(columns=80, rows=40, save_lines=10) as terminal:
        terminal.write(rows(*(b"" for _ in range(39)), b"   Hello World!"))
        terminal.select_start(3, 39)
        terminal.select_update(15, 39)
        fill_history(terminal, 7)
        expect(terminal.select_finish(), b"Hello World!", "scrolled selection")


MULTI = (
    b"   Hello World!",
    b"This is a copy test",
    b"for a selection with multiple lines.",
    b"All of them are on screen (not in the sb).------",
    b"Text not in SB",
    b"More Text not in SB",
)


def test_screen_copy_lines(factory):
    with factory(columns=80, rows=40) as terminal:
        terminal.write(rows(*MULTI[:4]))
        expect(
            selection(terminal, (3, 0), (42, 3)),
            b"Hello World!\nThis is a copy test\n"
            b"for a selection with multiple lines.\n"
            b"All of them are on screen (not in the sb).",
            "multi-line selection",
        )
        expect(
            selection(terminal, (0, 1), (15, 2)),
            b"This is a copy test\nfor a selection",
            "partial multi-line selection",
        )
        expect(
            selection(terminal, (42, 3), (3, 0)),
            b"Hello World!\nThis is a copy test\n"
            b"for a selection with multiple lines.\n"
            b"All of them are on screen (not in the sb).",
            "reverse multi-line selection",
        )


def test_screen_copy_lines_scrolled(factory):
    with factory(columns=80, rows=40, save_lines=10) as terminal:
        terminal.write(rows(*(b"" for _ in range(37)), *MULTI[:3]))
        terminal.select_start(3, 37)
        terminal.select_update(6, 39)
        fill_history(terminal, 7)
        expect(
            terminal.select_finish(),
            b"Hello World!\nThis is a copy test\nfor a ",
            "visible selection scrolled",
        )


def history_fixture(factory, save_lines=10):
    terminal = factory(columns=80, rows=40, save_lines=save_lines)
    terminal.write(rows(*MULTI))
    fill_history(terminal, 40)
    return terminal


def test_screen_copy_line_sb(factory):
    with history_fixture(factory) as terminal:
        terminal.wheel_up(6)
        checks = (
            ((3, 0), (15, 0), b"Hello World!"),
            ((3, 0), (8, 0), b"Hello"),
            ((0, 0), (8, 0), b"   Hello"),
            ((15, 0), (3, 0), b"Hello World!"),
            ((8, 0), (3, 0), b"Hello"),
        )
        for start, end, expected in checks:
            expect(
                selection(terminal, start, end),
                expected,
                "history line",
            )


def test_screen_copy_line_sb_scrolled(factory):
    with factory(columns=80, rows=40, save_lines=10) as terminal:
        terminal.write(b"   Hello World!")
        terminal.select_start(3, 0)
        terminal.select_update(15, 0)
        fill_history(terminal, 40)
        expect(terminal.select_finish(), b"Hello World!", "selection moved to history")
        terminal.wheel_up()
        terminal.select_start(3, 0)
        terminal.select_update(15, 0)
        fill_history(terminal, 3)
        expect(terminal.select_finish(), b"Hello World!", "history selection moved")


def test_screen_copy_line_sb_scrolled_invalid(factory):
    with factory(columns=80, rows=40, save_lines=0) as terminal:
        terminal.write(b"   Hello World!")
        terminal.select_start(3, 0)
        terminal.select_update(15, 0)
        fill_history(terminal, 40)
        expect(terminal.select_finish(), b"", "evicted selection")


def test_screen_copy_lines_sb(factory):
    with history_fixture(factory) as terminal:
        terminal.wheel_up(6)
        full = (
            b"Hello World!\nThis is a copy test\n"
            b"for a selection with multiple lines.\n"
            b"All of them are on screen (not in the sb)."
        )
        cross = (
            b"All of them are on screen (not in the sb).------\n"
            b"Text not in SB\nMore Text not in SB"
        )
        checks = (
            ((3, 0), (42, 3), full),
            ((0, 1), (15, 2), b"This is a copy test\nfor a selection"),
            ((42, 3), (3, 0), full),
            ((0, 3), (19, 5), cross),
            ((19, 5), (0, 3), cross),
            (
                (9, 5),
                (7, 3),
                b"them are on screen (not in the sb).------\n"
                b"Text not in SB\nMore Text",
            ),
        )
        for start, end, expected in checks:
            expect(
                selection(terminal, start, end),
                expected,
                "history multi-line selection",
            )


def test_screen_copy_lines_sb_scrolled(factory):
    with factory(columns=80, rows=40, save_lines=10) as terminal:
        terminal.write(rows(b"   Hello World!", b"Line 2", b"Line 3"))
        terminal.select_start(3, 0)
        terminal.select_update(6, 2)
        fill_history(terminal, 40)
        expect(
            terminal.select_finish(),
            b"Hello World!\nLine 2\nLine 3",
            "multi-line selection moved to history",
        )
        terminal.wheel_up(3)
        terminal.select_start(3, 0)
        terminal.select_update(6, 2)
        fill_history(terminal, 3)
        expect(
            terminal.select_finish(),
            b"Hello World!\nLine 2\nLine 3",
            "scrolled history selection moved again",
        )


def test_screen_copy_lines_sb_scrolled_cut_off(factory):
    with factory(columns=80, rows=40, save_lines=0) as terminal:
        terminal.write(rows(b"   Hello World!", b"Line 2", b"Line 3"))
        terminal.select_start(3, 0)
        terminal.select_update(6, 2)
        fill_history(terminal, 39)
        expect(terminal.select_finish(), b"Line 3", "cut selection")


def test_vte_init(factory):
    test_screen_init(factory)


def test_vte_null(factory):
    with factory() as terminal:
        terminal.write(b"")
        expect(terminal.read_input(), b"", "empty input")
        expect(terminal.conformance_state()["screen"], "Primary", "live VTE")


def set_custom_palette(terminal):
    terminal.write(b"\x1b]10;rgb:10/22/34\x1b\\")
    terminal.write(b"\x1b]11;rgb:11/23/35\x1b\\")
    for index in range(16):
        terminal.write(
            f"\x1b]4;{index};rgb:{index:02x}/{index + 18:02x}/{index + 36:02x}\x1b\\".encode()
        )


def test_vte_custom_palette(factory):
    with factory() as terminal:
        set_custom_palette(terminal)
        terminal.write(b"\x1b[31;40mX")
        cell = terminal.model_snapshot().cell(0, 0)
        expect(cell.foreground, (1, 19, 37), "custom foreground")
        expect(cell.background, (0, 18, 36), "custom background")


def test_vte_osc_query(factory):
    with factory() as terminal:
        set_custom_palette(terminal)
        queries = (
            (b"\x1b]10;?\x07", b"\x1b]10;rgb:1010/2222/3434\x1b\\"),
            (b"\x1b]10;?\x1b\\", b"\x1b]10;rgb:1010/2222/3434\x1b\\"),
            (
                b"\x1b]10;?;?\x07",
                b"\x1b]10;rgb:1010/2222/3434\x1b\\"
                b"\x1b]11;rgb:1111/2323/3535\x1b\\",
            ),
            (b"\x1b]11;?\x07", b"\x1b]11;rgb:1111/2323/3535\x1b\\"),
            (
                b"\x1b]11;?;?\x07",
                b"\x1b]11;rgb:1111/2323/3535\x1b\\"
                b"\x1b]12;rgb:ffff/ffff/ffff\x1b\\",
            ),
        )
        for query, expected in queries:
            terminal.write(query)
            expect(terminal.read_input(), expected, "OSC dynamic color query")


def test_vte_osc4(factory):
    with factory() as terminal:
        set_custom_palette(terminal)
        terminal.write(b"\x1b]4;1;?;13;?;3;?\x07")
        expect(
            terminal.read_input(),
            b"\x1b]4;1;rgb:0101/1313/2525\x1b\\"
            b"\x1b]4;13;rgb:0d0d/1f1f/3131\x1b\\"
            b"\x1b]4;3;rgb:0303/1515/2727\x1b\\",
            "OSC 4 multi-query",
        )
        terminal.write(b"\x1b]4;110;?;254;?;\x07")
        expect(
            terminal.read_input(),
            b"\x1b]4;110;rgb:8787/afaf/d7d7\x1b\\"
            b"\x1b]4;254;rgb:e4e4/e4e4/e4e4\x1b\\",
            "OSC 4 cube and grayscale query",
        )
        terminal.write(b"\x1b]4;1;rgb:11/22/33\x07\x1b]4;1;?\x07")
        expect(
            terminal.read_input(),
            b"\x1b]4;1;rgb:1111/2222/3333\x1b\\",
            "OSC 4 set and query",
        )


def test_vte_backspace_key(factory):
    with factory() as terminal:
        terminal.key("BACKSPACE")
        terminal.write(b"\x1b[?67h")
        terminal.key("BACKSPACE")
        terminal.write(b"\x1b[?67l")
        terminal.key("BACKSPACE")
        expect(terminal.read_input(), b"\x7f\x08\x7f", "DECBKM")


def test_vte_get_flags(factory):
    with factory() as terminal:
        expect(terminal.conformance_state()["DECCKM"], False, "DECCKM reset")
        terminal.write(b"\x1b[?1h")
        expect(terminal.conformance_state()["DECCKM"], True, "DECCKM set")


def test_vte_decrqm_no_reset(factory):
    with factory() as terminal:
        terminal.write(b"\x1b[?1049h\x1b[?12$p")
        expect(terminal.conformance_state()["screen"], "Alternate", "DECRQM screen")


def test_vte_csi_cursor_up_down(factory):
    with factory(columns=80, rows=24) as terminal:
        terminal.write(b"\n123")
        expect((terminal.model_snapshot().cursor_x, terminal.model_snapshot().cursor_y), (3, 1), "initial cursor")
        terminal.write(b"\x1b[1F")
        expect((terminal.model_snapshot().cursor_x, terminal.model_snapshot().cursor_y), (0, 0), "CPL")
        terminal.write(b"\x1b[1E")
        expect((terminal.model_snapshot().cursor_x, terminal.model_snapshot().cursor_y), (0, 1), "CNL")
        terminal.write(b"\x1b[34F")
        expect(terminal.model_snapshot().cursor_y, 0, "CPL clamp")
        terminal.write(b"\x1b[34E")
        expect(terminal.model_snapshot().cursor_y, 23, "CNL clamp")


def test_mouse_cb_x10(factory):
    with factory() as terminal:
        terminal.write(b"\x1b[?9h")
        expect(terminal.state()[:2], (1, 0), "X10 tracking state")


def test_mouse_x10(factory):
    with factory() as terminal:
        terminal.write(b"\x1b[?9h")
        checks = (
            ((0, 0, 0, 0, 1, 1, 1), b"\x1b[M !!"),
            ((0, 0, 0, 0, 3, 1, 1), b'\x1b[M"!!'),
            ((0, 0, 0, 0, 2, 1, 1), b"\x1b[M!!!"),
            ((0, 0, 0, 0, 1, 300, 280), b"\x1b[M \xff\xff"),
        )
        for arguments, expected in checks:
            expect(terminal.mouse_encode(*arguments), expected, "X10 encoding")


def test_mouse_cb_sgr(factory):
    with factory() as terminal:
        terminal.write(b"\x1b[?1006h")
        expect(terminal.state()[:2], (0, 2), "SGR encoding only")
        terminal.write(b"\x1b[?1002h")
        expect(terminal.state()[:2], (3, 2), "SGR button tracking")


def test_mouse_sgr(factory):
    with factory() as terminal:
        terminal.write(b"\x1b[?1006h\x1b[?1002h")
        checks = (
            ((2, 0, 0, 0, 1, 1, 1), b"\x1b[<0;1;1M"),
            ((2, 1, 0, 0, 1, 1, 1), b"\x1b[<0;1;1m"),
            ((2, 0, 0, 0, 2, 1, 1), b"\x1b[<1;1;1M"),
            ((2, 0, 0, 0, 3, 1, 1), b"\x1b[<2;1;1M"),
            ((2, 0, 0, 0, 4, 1, 1), b"\x1b[<64;1;1M"),
            ((2, 0, 0, 0, 5, 1, 1), b"\x1b[<65;1;1M"),
            ((2, 0, 0, 0, 1, 50, 120), b"\x1b[<0;50;120M"),
        )
        for arguments, expected in checks:
            expect(terminal.mouse_encode(*arguments), expected, "SGR encoding")


def test_mouse_sgr_cell_change(factory):
    with factory(columns=10, rows=4) as terminal:
        terminal.write(b"\x1b[?1006h\x1b[?1003h")
        terminal.pointer(2, 2)
        expect(terminal.read_input(), b"\x1b[<35;1;1M", "first motion")
        terminal.pointer(2, 2)
        expect(terminal.read_input(), b"", "duplicate cell motion")
        terminal.pointer(3, 3)
        expect(terminal.read_input(), b"\x1b[<35;2;2M", "new cell motion")
        terminal.button(0, True, x=3, y=3)
        expect(terminal.read_input(), b"\x1b[<0;2;2M", "same-cell click")


def test_mouse_cb_pixels(factory):
    with factory() as terminal:
        terminal.write(b"\x1b[?1016h")
        expect(terminal.state()[:2], (0, 4), "pixel encoding only")
        terminal.write(b"\x1b[?1003h")
        expect(terminal.state()[:2], (4, 4), "pixel any-event tracking")


def test_mouse_pixels(factory):
    with factory() as terminal:
        terminal.write(b"\x1b[?1016h\x1b[?1003h")
        checks = (
            ((4, 2, 0, 0, 1, 236, 120), b"\x1b[<35;236;120M"),
            ((4, 0, 0, 0, 1, 236, 120), b"\x1b[<0;236;120M"),
            ((4, 1, 0, 0, 1, 236, 120), b"\x1b[<0;236;120m"),
        )
        for arguments, expected in checks:
            expect(terminal.mouse_encode(*arguments), expected, "pixel encoding")


CASES = {name: globals()[name] for name in CASE_NAMES}


def run_case(name, factory):
    try:
        case = CASES[name]
    except KeyError as error:
        raise KeyError(f"unknown libtsm semantic case: {name}") from error
    case(factory)
