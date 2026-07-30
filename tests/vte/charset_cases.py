#!/usr/bin/env python3

UTF8 = 0
DEC_SPECIAL = 1
DEC_SUPPLEMENTAL = 2
DEC_USER_PREFERRED = 3
DEC_TECHNICAL = 4
ISO_LATIN1 = 5
ISO_UK = 6
NRC_DUTCH = 7
NRC_FINNISH = 8
NRC_FRENCH = 9
NRC_FRENCH_CANADIAN = 10
NRC_GERMAN = 11
NRC_ITALIAN = 12
NRC_NORWEGIAN_DANISH = 13
NRC_PORTUGUESE = 14
NRC_SPANISH = 15
NRC_SWEDISH = 16
NRC_SWISS = 17
NRC_GREEK = 18
NRC_HEBREW = 19
NRC_RUSSIAN = 20
NRC_SERBO_CROATIAN = 21
NRC_TURKISH = 22

CASE_NAMES = (
    "94",
    "96",
    "94_n",
    "96_n",
    "control",
    "other",
)

FINALS = range(0x30, 0x7f)
BASE_STATE = (DEC_SPECIAL,) * 4
PRESEED = b"\x1b(0\x1b)0\x1b*0\x1b+0"


def with_slot(slot, charset):
    state = list(BASE_STATE)
    state[slot] = charset
    return tuple(state)


def charset_94(final, modifier):
    if not modifier:
        return {
            ord("A"): ISO_UK,
            ord("0"): DEC_SPECIAL,
            ord("5"): NRC_FINNISH,
            ord("C"): NRC_FINNISH,
            ord("<"): DEC_USER_PREFERRED,
            ord(">"): DEC_TECHNICAL,
            ord("4"): NRC_DUTCH,
            ord("R"): NRC_FRENCH,
            ord("f"): NRC_FRENCH,
            ord("9"): NRC_FRENCH_CANADIAN,
            ord("Q"): NRC_FRENCH_CANADIAN,
            ord("K"): NRC_GERMAN,
            ord("Y"): NRC_ITALIAN,
            ord("`"): NRC_NORWEGIAN_DANISH,
            ord("E"): NRC_NORWEGIAN_DANISH,
            ord("6"): NRC_NORWEGIAN_DANISH,
            ord("Z"): NRC_SPANISH,
            ord("7"): NRC_SWEDISH,
            ord("H"): NRC_SWEDISH,
            ord("="): NRC_SWISS,
        }.get(final, UTF8)
    if modifier == b"%":
        return {
            ord("2"): NRC_TURKISH,
            ord("3"): NRC_SERBO_CROATIAN,
            ord("5"): DEC_SUPPLEMENTAL,
            ord("6"): NRC_PORTUGUESE,
            ord("="): NRC_HEBREW,
        }.get(final, UTF8)
    if modifier == b"&" and final == ord("5"):
        return NRC_RUSSIAN
    if modifier == b'"' and final == ord(">"):
        return NRC_GREEK
    return UTF8


def charset_96(final, modifier):
    if modifier:
        return UTF8
    return {
        ord("A"): ISO_LATIN1,
        ord("<"): DEC_USER_PREFERRED,
    }.get(final, UTF8)


def family_94():
    modifiers = (b"", b" ", b"!", b'"', b"#", b"%", b"&", b"'")
    for slot, prefix in enumerate((b"(", b")", b"*", b"+")):
        for modifier in modifiers:
            for final in FINALS:
                yield (
                    b"\x1b" + prefix + modifier + bytes((final,)),
                    with_slot(slot, charset_94(final, modifier)),
                )


def family_96():
    modifiers = (b"", b" ", b"!", b'"', b"#", b"&", b"'")
    for slot, prefix in enumerate((b"-", b".", b"/"), 1):
        for modifier in modifiers:
            for final in FINALS:
                yield (
                    b"\x1b" + prefix + modifier + bytes((final,)),
                    with_slot(slot, charset_96(final, modifier)),
                )


def family_94_n():
    modifiers = (b"", b" ", b"!", b'"', b"#", b"%", b"&", b"'")
    for prefix in (b"$(", b"$)", b"$*", b"$+"):
        for modifier in modifiers:
            for final in FINALS:
                yield b"\x1b" + prefix + modifier + bytes((final,)), BASE_STATE
    for final in b"@AB":
        yield b"\x1b$" + bytes((final,)), BASE_STATE


def family_96_n():
    modifiers = (b"", b" ", b"!", b'"', b"#", b"$", b"%", b"&", b"'")
    for prefix in (b"$-", b"$.", b"$/"):
        for modifier in modifiers:
            for final in FINALS:
                yield b"\x1b" + prefix + modifier + bytes((final,)), BASE_STATE


def family_control():
    for prefix in (b"!", b'"'):
        for final in FINALS:
            yield b"\x1b" + prefix + bytes((final,)), BASE_STATE


def family_other():
    for prefix in (b"%", b"% ", b"%/"):
        for final in FINALS:
            state = BASE_STATE
            if prefix == b"%" and final == ord("@"):
                state = (UTF8, UTF8, ISO_LATIN1, ISO_LATIN1)
            elif prefix == b"%" and final == ord("G"):
                state = (UTF8,) * 4
            yield b"\x1b" + prefix + bytes((final,)), state


def cases(name):
    return {
        "94": family_94,
        "96": family_96,
        "94_n": family_94_n,
        "96_n": family_96_n,
        "control": family_control,
        "other": family_other,
    }[name]()
