#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.


def drag(terminal, start_x, start_y, end_x, end_y):
    terminal.select_start(start_x, start_y)
    terminal.select_update(end_x + 1, end_y)
    return terminal.select_finish()


def click_n(terminal, column, row, button, count, timestamp):
    result = b""
    for click in range(count):
        when = timestamp + click * 0.1
        result = terminal.button(
            button,
            True,
            x=column + 2,
            y=row + 2,
            time=when,
        )
        result = terminal.button(
            button,
            False,
            x=column + 2,
            y=row + 2,
            time=when + 0.01,
        )
    return result


def drag_case(name, start, end, expected):
    def run(terminal):
        terminal.write("hello world\r\n💀skull\r\n".encode())
        return drag(terminal, *start, *end)

    return name, "drag_selection", 3, 12, 0, run, expected


def click_case(name, label, column, count, expected):
    def run(terminal):
        terminal.write(b"hello world")
        return click_n(terminal, column, 0, 0, count, 1.0)

    return name, label, 3, 10, 0, run, expected


def scrollback_case(name, operation, expected):
    def run(terminal):
        terminal.write(b"1 2 3 4")
        terminal.wheel_up()
        return operation(terminal)

    return name, "selection_in_scrollback", 2, 2, 4, run, expected


def scrollback_word(terminal):
    return click_n(terminal, 0, 0, 0, 2, 1.0)


def scrollback_line(terminal):
    click_n(terminal, 0, 0, 0, 2, 1.0)
    click_n(terminal, 0, 1, 1, 1, 2.0)
    return click_n(terminal, 0, 1, 0, 3, 3.0)


def scrollback_drag(terminal):
    click_n(terminal, 0, 0, 0, 2, 1.0)
    click_n(terminal, 0, 1, 1, 1, 2.0)
    click_n(terminal, 0, 1, 0, 3, 3.0)
    return drag(terminal, 0, 0, 0, 1)


CASES = (
    drag_case("selection_0012", (1, 0), (4, 0), b"ello"),
    drag_case("selection_0022", (1, 1), (5, 1), b"skul"),
    drag_case("selection_0026", (0, 1), (5, 1), "💀skul".encode()),
    drag_case("selection_0030", (1, 0), (6, 1), "ello world\n💀skull".encode()),
    drag_case("selection_0036", (0, 0), (15, 3), "hello world\n💀skull".encode()),
    drag_case("selection_0042", (6, 0), (3, 1), "world\n💀sk".encode()),
    click_case("selection_0053", "double_click_selection", 1, 2, b"hello"),
    click_case("selection_0064", "triple_click_selection", 1, 3, b"hello world"),
    click_case("selection_0075", "double_click_wrapped_selection", 7, 2, b"world"),
    scrollback_case("selection_0090", scrollback_word, b"2"),
    scrollback_case("selection_0096", scrollback_line, b"1 2 3 4"),
    scrollback_case("selection_0099", scrollback_drag, b"2 3"),
)


def case_names():
    return tuple(case[0] for case in CASES)


def case_data(name):
    for case in CASES:
        if case[0] == name:
            return case[1:]
    raise KeyError(name)
