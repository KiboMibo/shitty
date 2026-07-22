"""Offline projections of the live probes in upstream ucs-detect."""


XTGETTCAP_CAPABILITIES = (
    "TN", "Co", "RGB", "colors", "pairs", "bce", "ccc", "npc", "xenl",
    "acsc", "sgr", "setab", "setaf", "sitm", "smcup", "rmcup", "kmous",
    "is2", "rs1", "u6", "u7", "u8", "u9", "kcuu1", "kcud1", "kcub1",
    "kcuf1", "khome", "kend", "knp", "kpp", "kich1", "kdch1", "kbs",
    "kcbt", "ka1", "ka3", "kb2", "kc1", "kc3", "kf1", "kf2", "kf3",
    "kf4", "kf5", "kf6", "kf7", "kf8", "kf9", "kf10", "kf11",
    "kf12",
)


DEC_MODES = {
    "bracketed_paste": 2004,
    "synchronized_output": 2026,
    "grapheme_clustering": 2027,
    "in_band_window_resize": 2048,
    "focus_in_out_events": 1004,
    "mouse_extended_sgr": 1006,
    "bracketed_paste_mime": 5522,
    "color_palette_updates": 2031,
}


DECRQSS_SETTINGS = {
    "sgr": b"m",
    "decscusr": b" q",
    "decstbm": b"r",
    "decslrm": b"s",
    "decscl": b'"p',
    "decsca": b'"q',
    "decscpp": b"$|",
    "decslpp": b"t",
    "decsnls": b"*|",
    "decsasd": b"$}",
    "decssdt": b"$~",
    "decsace": b"*x",
}


SIMPLE_CASES = (
    "device_attributes",
    "software",
    "foreground_color",
    "background_color",
    "cell_size",
    "pixel_size",
    "tab_stop_width",
    "kitty_keyboard",
    "color_scheme",
    "decrqss_truecolor",
    "decrqcra",
    "osc52_clipboard",
    "styled_underlines",
    "screenleak_xtversion",
    "screenleak_xtgettcap",
)


def case_names():
    yield from SIMPLE_CASES
    for name in DEC_MODES:
        yield "mode_" + name
    for name in DECRQSS_SETTINGS:
        yield "decrqss_" + name
    for name in XTGETTCAP_CAPABILITIES:
        yield "xtgettcap_" + name
