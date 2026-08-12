#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.


CASE_NAMES = (
    "testParse",
    "testBufferedUpdates",
    "testKittyKeyboardPushPopQuery",
    "testKittyKeyboardSet",
    "testKittyKeyboardReset",
    "testKittyKeyboardDisambiguate",
    "testKittyKeyboardEventTypes",
    "testKittyKeyboardReportAllKeys",
    "testKittyKeyboardLegacyKeys",
    "testKittyKeyboardCtrlLetters",
    "testKittyKeyboardTextKeys",
)


def case_names():
    return CASE_NAMES


def expect(actual, expected, subject):
    if actual != expected:
        raise AssertionError(
            f"{subject}: expected {expected!r}, got {actual!r}"
        )


def write_read(terminal, payload):
    terminal.write(payload)
    return terminal.read_input()


def testParse(terminal):
    terminal.write(b"a")
    expect(terminal.model_snapshot().lines[0][0], "a", "printed ASCII")
    expect(
        write_read(terminal, b"\x1b[=0c"),
        b"\x1bP!|00000000\x1b\\",
        "tertiary device attributes",
    )


def testBufferedUpdates(terminal):
    before = terminal.snapshot().refresh_count
    terminal.write(b"hello!")
    expect(
        terminal.snapshot().refresh_count > before,
        True,
        "ordinary buffered update",
    )

    before = terminal.snapshot().refresh_count
    terminal.write(b"\x1b[?2026hhidden")
    expect(
        terminal.snapshot().refresh_count,
        before,
        "synchronized update is held",
    )
    terminal.sync_timeout()
    expect(
        terminal.snapshot().refresh_count > before,
        True,
        "synchronized update timeout",
    )

    before = terminal.snapshot().refresh_count
    terminal.write(b"\x1b[?2026hvisible\x1b[?2026l")
    expect(
        terminal.snapshot().refresh_count,
        before + 1,
        "synchronized begin/end publishes once",
    )


def testKittyKeyboardPushPopQuery(terminal):
    expect(write_read(terminal, b"\x1b[?u"), b"\x1b[?0u", "initial flags")
    expect(
        write_read(terminal, b"\x1b[>1u\x1b[?u"),
        b"\x1b[?1u",
        "first pushed flags",
    )
    expect(
        write_read(terminal, b"\x1b[>3u\x1b[?u"),
        b"\x1b[?3u",
        "second pushed flags",
    )
    expect(
        write_read(terminal, b"\x1b[<1u\x1b[?u"),
        b"\x1b[?1u",
        "first pop",
    )
    expect(
        write_read(terminal, b"\x1b[<1u\x1b[?u"),
        b"\x1b[?0u",
        "second pop",
    )
    expect(
        write_read(terminal, b"\x1b[<1u\x1b[?u"),
        b"\x1b[?0u",
        "empty pop",
    )


def testKittyKeyboardSet(terminal):
    terminal.write(b"\x1b[>0u")
    expect(
        write_read(terminal, b"\x1b[=5;1u\x1b[?u"),
        b"\x1b[?5u",
        "replace flags",
    )
    expect(
        write_read(terminal, b"\x1b[=2;2u\x1b[?u"),
        b"\x1b[?7u",
        "OR flags",
    )
    expect(
        write_read(terminal, b"\x1b[=4;3u\x1b[?u"),
        b"\x1b[?3u",
        "AND-NOT flags",
    )


def testKittyKeyboardReset(terminal):
    expect(
        write_read(terminal, b"\x1b[>5u\x1b[?u"),
        b"\x1b[?5u",
        "flags before reset",
    )
    expect(
        write_read(terminal, b"\x1bc\x1b[?u"),
        b"\x1b[?0u",
        "flags after RIS",
    )


def testKittyKeyboardDisambiguate(terminal):
    terminal.write(b"\x1b[>1u")
    terminal.frontend_key_event(256, 1, modifiers=2)
    expect(terminal.read_input(), b"\x1b[27;5u", "Ctrl+Escape")


def testKittyKeyboardEventTypes(terminal):
    terminal.write(b"\x1b[>3u")
    terminal.kitty_key(ord("a"), event=3)
    expect(terminal.read_input(), b"\x1b[97;1:3u", "key release")


def testKittyKeyboardReportAllKeys(terminal):
    terminal.write(b"\x1b[>9u")
    terminal.kitty_key(ord("a"))
    terminal.frontend_key_event(256, 1)
    expect(
        terminal.read_input(),
        b"\x1b[97u\x1b[27u",
        "all keys reported",
    )


def testKittyKeyboardLegacyKeys(terminal):
    terminal.write(b"\x1b[>1u")
    checks = (
        ("UP", 0, b"\x1b[A"),
        ("UP", 4, b"\x1b[1;5A"),
        ("F5", 0, b"\x1b[15~"),
        ("F1", 0, b"\x1b[P"),
        ("F1", 1, b"\x1b[1;2P"),
        ("DELETE", 0, b"\x1b[3~"),
    )
    for name, modifiers, expected in checks:
        terminal.kitty_special(name, modifiers=modifiers)
        expect(terminal.read_input(), expected, f"{name}/{modifiers}")


def testKittyKeyboardCtrlLetters(terminal):
    terminal.write(b"\x1b[>1u")
    for character in ("c", "d"):
        terminal.kitty_key(ord(character), modifiers=4)
        expect(
            terminal.read_input(),
            f"\x1b[{ord(character)};5u".encode(),
            f"Ctrl+{character.upper()}",
        )


def testKittyKeyboardTextKeys(terminal):
    terminal.write(b"\x1b[>1u")
    checks = (
        (ord("A"), ord("a"), 2, False, b"\x1b[97;5u"),
        (ord("A"), ord("a"), 4, True, b"\x1b[97;3u"),
        (ord("A"), ord("A"), 3, False, b"\x1b[97;6u"),
        (ord("A"), ord("a"), 0, True, b"a"),
        (ord("A"), ord("A"), 1, True, b"A"),
        (ord(" "), ord(" "), 0, True, b" "),
    )
    for key, text, modifiers, emits_text, expected in checks:
        terminal.frontend_key_event(key, 1, modifiers=modifiers)
        if emits_text:
            terminal.frontend_text_event(text, modifiers=modifiers)
        expect(terminal.read_input(), expected, f"text key {key}/{modifiers}")

    terminal.write(b"\x1b[=8;1u")
    checks = (
        (ord("A"), ord("a"), 0, b"\x1b[97u"),
        (ord("A"), ord("A"), 1, b"\x1b[97;2u"),
        (ord(" "), ord(" "), 0, b"\x1b[32u"),
    )
    for key, text, modifiers, expected in checks:
        terminal.frontend_key_event(key, 1, modifiers=modifiers)
        terminal.frontend_text_event(text, modifiers=modifiers)
        expect(terminal.read_input(), expected, f"report-all key {key}/{modifiers}")


CASES = {name: globals()[name] for name in CASE_NAMES}


def run_case(name, terminal):
    try:
        case = CASES[name]
    except KeyError as error:
        raise KeyError(f"unknown Konsole Vt102 case: {name}") from error
    case(terminal)
