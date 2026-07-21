#!/usr/bin/env python3

import re

from backend import Backend


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def plain_text():
    with Backend() as backend:
        backend.feed("Hello, world!")
        require("Hello, world!" in backend.text(), "plain text is absent")


def multiline():
    with Backend() as backend:
        backend.feed("Line 1\r\nLine 2\r\nLine 3")
        text = backend.text()
        require(all(line in text for line in ("Line 1", "Line 2", "Line 3")), "multiline text differs")


def cursor_positioning():
    with Backend(40, 10) as backend:
        backend.feed("\x1b[3;10HX")
        require(backend.cell(2, 9).char == "X", "CUP cell differs")


def line_wrap_at_boundary():
    with Backend() as backend:
        backend.feed("1234567890" * 8 + "WRAP")
        require("WRAP" in backend.text(), "boundary text did not wrap")


def cell_case(data, check, message):
    with Backend() as backend:
        backend.feed(data)
        require(check(backend.cell(0, 0)), message)


def bold():
    with Backend() as backend:
        backend.feed("\x1b[1mB\x1b[0mN")
        require(backend.cell(0, 0).bold and not backend.cell(0, 1).bold, "bold state differs")


def italic():
    cell_case("\x1b[3mI\x1b[0m", lambda c: c.italic, "italic is absent")


def faint():
    cell_case("\x1b[2mF\x1b[0m", lambda c: c.faint, "faint is absent")


def strikethrough():
    cell_case("\x1b[9mS\x1b[0m", lambda c: c.strike, "strikethrough is absent")


def inverse():
    cell_case("\x1b[7mI\x1b[0m", lambda c: c.inverse, "inverse is absent")


def truecolor_foreground():
    cell_case("\x1b[38;2;255;128;0mR\x1b[0m", lambda c: c.foreground == (255, 128, 0), "foreground RGB differs")


def truecolor_background():
    cell_case("\x1b[48;2;0;128;255mB\x1b[0m", lambda c: c.background == (0, 128, 255), "background RGB differs")


def combined_styles():
    cell_case("\x1b[1;3;38;2;255;0;0mX\x1b[0m", lambda c: c.bold and c.italic and c.foreground == (255, 0, 0), "combined style differs")


def sgr_reset_clears_all_styles():
    with Backend() as backend:
        backend.feed("\x1b[1;3;4;9mStyled\x1b[0mPlain")
        cell = backend.cell(0, 6)
        require(not (cell.bold or cell.italic or cell.strike or cell.underline), "SGR 0 left attributes")


def indexed_foreground():
    cell_case("\x1b[38;5;196mR\x1b[0m", lambda c: c.foreground_index == 196, "indexed foreground differs")


def cursor_after_text():
    with Backend() as backend:
        backend.feed("Hello")
        require(backend.cursor() == (5, 0), "cursor after text differs")


def cursor_after_newline():
    with Backend() as backend:
        backend.feed("Line1\r\nLine2")
        require(backend.cursor() == (5, 1), "cursor after newline differs")


def cursor_after_cup():
    with Backend() as backend:
        backend.feed("\x1b[10;20H")
        require(backend.cursor() == (19, 9), "cursor after CUP differs")


def cursor_forward():
    with Backend() as backend:
        backend.feed("\x1b[5C")
        require(backend.cursor()[0] == 5, "CUF position differs")


def alt_screen_toggle():
    with Backend() as backend:
        require(not backend.mode("altScreen"), "alt screen initially active")
        backend.feed("\x1b[?1049h")
        require(backend.mode("altScreen"), "alt screen did not activate")
        backend.feed("\x1b[?1049l")
        require(not backend.mode("altScreen"), "alt screen did not deactivate")


def bracketed_paste():
    with Backend() as backend:
        backend.feed("\x1b[?2004h")
        require(backend.bracketed_paste_enabled(), "bracketed paste is inactive")


def auto_wrap_default():
    with Backend() as backend:
        require(backend.mode("autoWrap"), "auto wrap is not enabled by default")


def emoji_width():
    with Backend() as backend:
        backend.feed("🎉A")
        require(backend.cell(0, 0).double_width, "emoji is not wide")
        require(backend.cell(0, 2).char == "A", "cell after emoji differs")


def cjk_width():
    with Backend() as backend:
        backend.feed("漢A")
        require(backend.cell(0, 0).double_width, "CJK character is not wide")
        require(backend.cell(0, 2).char == "A", "cell after CJK differs")


def single_underline():
    cell_case("\x1b[4mU\x1b[0m", lambda c: c.underline and c.underline_style == 1, "single underline differs")


def application_cursor():
    with Backend() as backend:
        backend.feed("\x1b[?1h")
        require(backend.mode("applicationCursor"), "DECCKM is inactive")


def osc_2_title():
    with Backend() as backend:
        backend.feed("\x1b]2;My Title\x07")
        require(backend.title == "My Title", "OSC 2 title differs")


def mouse_tracking():
    with Backend() as backend:
        backend.feed("\x1b[?1000h")
        require(backend.mode("mouseTracking"), "mouse tracking is inactive")


def focus_tracking():
    with Backend() as backend:
        backend.feed("\x1b[?1004h")
        require(backend.mode("focusTracking"), "focus tracking is inactive")


def resize_preserves_content():
    with Backend(40, 10) as backend:
        backend.feed("Before")
        backend.resize(80, 24)
        require("Before" in backend.text(), "resize lost content")


def reset_clears_content():
    with Backend() as backend:
        backend.feed("Content here")
        backend.reset()
        require("Content here" not in backend.text(), "reset retained content")


def ris_clears_screen():
    with Backend() as backend:
        backend.feed("Content\x1bc")
        require("Content" not in backend.text(), "RIS retained content")


def screen_lines():
    with Backend() as backend:
        require(backend.scrollback().screen_lines == 24, "screen line count differs")


def scrollback_accumulates():
    with Backend() as backend:
        backend.feed("\r\n".join(f"Line {index}" for index in range(30)))
        require(backend.scrollback().total_lines > 24, "scrollback did not accumulate")


def truecolor_capability():
    truecolor_foreground()


def reflow_capability():
    with Backend(8, 3) as backend:
        backend.feed("abcdefghijkl")
        backend.resize(6, 3)
        text = backend.text().replace("\n", "")
        require("abcdefghijkl" in text, "resize did not reflow content")


def kitty_keyboard_capability():
    with Backend() as backend:
        backend.feed("\x1b[>7u\x1b[?u")
        require(backend.terminal.state()[3] == 7, "kitty flags differ")
        require(backend.terminal.read_input() == b"\x1b[?7u", "kitty query reply differs")


def key_enter():
    with Backend() as backend:
        require(backend.encode_key("Enter") == b"\r", "Enter encoding differs")


def key_escape():
    with Backend() as backend:
        require(backend.encode_key("Escape") == b"\x1b", "Escape encoding differs")


def key_ctrl_c():
    with Backend() as backend:
        require(backend.encode_key("c", ctrl=True) == b"\x03", "Ctrl+C encoding differs")


def key_arrow_up():
    with Backend() as backend:
        require(backend.encode_key("ArrowUp") == b"\x1b[A", "ArrowUp encoding differs")


def compare_chunking(data, columns=40, rows=10):
    with Backend(columns, rows) as whole, Backend(columns, rows) as chunked:
        whole.feed(data)
        for byte in data.encode():
            chunked.feed(bytes((byte,)))
        require(whole.terminal.model_digest() == chunked.terminal.model_digest(), "whole and bytewise states differ")
        return whole, chunked


def same_text_rendering():
    compare_chunking("Hello, \x1b[1mworld\x1b[0m!\r\nLine 2 with \x1b[38;2;255;0;0mred\x1b[0m text")


def same_cursor_position():
    compare_chunking("Hello\r\n\x1b[5Cworld", 80, 24)


def same_style_attributes():
    compare_chunking("\x1b[1;3;38;2;100;200;50mStyled\x1b[0m Plain", 80, 24)


def window_text_area_pixels():
    with Backend() as backend:
        response = backend.response(b"\x1b[14t")
        match = re.fullmatch(rb"\x1b\[4;(\d+);(\d+)t", response)
        require(match is not None, f"CSI 14t reply differs: {response!r}")
        height, width = map(int, match.groups())
        require(height > 24 and width > 80, "pixel size is not non-trivial")
        require(width % 80 == 0 and height % 24 == 0, "pixel size is not grid divisible")


def window_cell_count():
    with Backend() as backend:
        require(backend.response(b"\x1b[18t") == b"\x1b[8;24;80t", "CSI 18t reply differs")


def color_scheme():
    with Backend() as backend:
        require(backend.response(b"\x1b[?996n") == b"\x1b[?997;1n", "color-scheme reply differs")


CASES = {name: value for name, value in globals().items() if callable(value) and not name.startswith("_")}
