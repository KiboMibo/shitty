/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "terminal_colors.h"

#include <type_traits>

using namespace stl;

static_assert(std::is_trivial_v<TerminalColorScheme>);
static_assert(std::is_standard_layout_v<TerminalColorScheme>);

namespace {
#include "terminal_colors.json.h"

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
    for (const auto& scheme : terminalColorSchemes) {
        if (equalAsciiCaseInsensitive(StringView(scheme.name), name)) {
            return &scheme;
        }
    }
    return nullptr;
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
