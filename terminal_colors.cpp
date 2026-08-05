/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "terminal_colors.h"

#include <std/typ/intrin.h>

using namespace stl;

static_assert(stdIsTrivial(TerminalColorScheme));
static_assert(stdIsStandardLayout(TerminalColorScheme));

namespace {
#include "terminal_colors.json.h"

    // The pre-brand defaults - xterm's classic colors on plain black.
    // Lives here rather than in the catalog: terminal_colors.json is
    // regenerated wholesale from the upstream collection.
    constexpr TerminalColorScheme builtinSchemes[] = {
        {
            "classic",
            {0xff, 0xff, 0xff},
            {0x00, 0x00, 0x00},
            {
                {0x00, 0x00, 0x00},
                {0xcd, 0x00, 0x00},
                {0x00, 0xcd, 0x00},
                {0xcd, 0xcd, 0x00},
                {0x00, 0x00, 0xee},
                {0xcd, 0x00, 0xcd},
                {0x00, 0xcd, 0xcd},
                {0xe5, 0xe5, 0xe5},
                {0x7f, 0x7f, 0x7f},
                {0xff, 0x00, 0x00},
                {0x00, 0xff, 0x00},
                {0xff, 0xff, 0x00},
                {0x5c, 0x5c, 0xff},
                {0xff, 0x00, 0xff},
                {0x00, 0xff, 0xff},
                {0xff, 0xff, 0xff},
            },
        },
    };

    constexpr u8 asciiLower(u8 ch) noexcept {
        return ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch;
    }

    bool equalAsciiCaseInsensitive(StringView left, StringView right) noexcept {
        if (left.length() != right.length()) {
            return false;
        }
        for (size_t index = 0; index < left.length(); ++index) {
            if (asciiLower(left[index]) != asciiLower(right[index])) {
                return false;
            }
        }
        return true;
    }
}

const TerminalColorScheme* TerminalColorScheme::find(StringView name) noexcept {
    for (const auto& scheme : builtinSchemes) {
        if (equalAsciiCaseInsensitive(StringView(scheme.name), name)) {
            return &scheme;
        }
    }
    for (const auto& scheme : terminalColorSchemes) {
        if (equalAsciiCaseInsensitive(StringView(scheme.name), name)) {
            return &scheme;
        }
    }
    return nullptr;
}

const TerminalColorScheme* TerminalColorScheme::builtins() noexcept {
    return builtinSchemes;
}

size_t TerminalColorScheme::builtinCount() noexcept {
    return sizeof(builtinSchemes) / sizeof(builtinSchemes[0]);
}

const TerminalColorScheme* TerminalColorScheme::all() noexcept {
    return terminalColorSchemes;
}

size_t TerminalColorScheme::count() noexcept {
    return sizeof(terminalColorSchemes) / sizeof(terminalColorSchemes[0]);
}

Color TerminalColorScheme::foregroundColor() const noexcept {
    return {foreground[0], foreground[1], foreground[2]};
}

Color TerminalColorScheme::backgroundColor() const noexcept {
    return {background[0], background[1], background[2]};
}

AnsiPalette TerminalColorScheme::ansiPalette() const noexcept {
    AnsiPalette result;
    for (size_t index = 0; index < AnsiPalette::colorCount; ++index) {
        result[index] = {ansi[index][0], ansi[index][1], ansi[index][2]};
    }
    return result;
}
