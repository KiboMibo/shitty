/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "color.h"
#include "unicode_width.h"

#include <std/str/view.h>
#include <std/sys/types.h>

struct Darts;

// The semantic configuration of the VT core: every knob the terminal
// state machine reads. The embedder owns and fills it - in the shitty
// binaries Options carries one and the parser writes it - and the core
// only reads. Strings live in the owner's pool, NUL terminated.
struct VtConfig {
    u8 modifyOtherKeys = 0;
    u16 saveLines = 0;
    // The width emulation resolved from -unicodeWidths; parsing probes
    // the system libc when the option asks to match it.
    UnicodeWidths widths{0};
    // Lowercased URI schemes a plain-text link may use, interned as a
    // trie by the owner.
    const Darts* uriSchemeTrie = nullptr;
    stl::StringView title;
    stl::StringView dump;
    Color bg{};
    Color cr{};
    Color fg{};
    bool altScrollMode = false;
    bool altSendsEscape = false;
    bool autoCopyMode = false;
    bool allowOsc52Read = false;
    bool allowWindowOps = false;
    bool osc52SelectClipboard = false;
    bool boldColors = false;
    bool kittyCtrlBaseLayout = false;
    bool verbose = false;

    bool uriSchemeAllowed(stl::StringView scheme) const;
};
