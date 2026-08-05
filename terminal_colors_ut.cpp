/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "terminal_colors.h"

#include <std/str/view.h>
#include <std/tst/ut.h>

using namespace stl;

STD_TEST_SUITE(TerminalColorScheme) {
    STD_TEST(FindsImportedSchemesCaseInsensitively) {
        STD_INSIST(TerminalColorScheme::count() >= 1700);

        const TerminalColorScheme* scheme = TerminalColorScheme::find(StringView(u8"3024 night"));
        STD_INSIST(scheme != nullptr);
        STD_INSIST(StringView(scheme->name) == StringView(u8"3024 Night"));
        STD_INSIST((scheme->foregroundColor() == Color{0xa5, 0xa2, 0xa2}));
        STD_INSIST((scheme->backgroundColor() == Color{0x09, 0x03, 0x00}));
        const AnsiPalette ansi = scheme->ansiPalette();
        STD_INSIST((ansi[1] == Color{0xdb, 0x2d, 0x20}));
        STD_INSIST((ansi[15] == Color{0xf7, 0xf7, 0xf7}));
    }

    STD_TEST(FindsCanonicalTerminalDefaults) {
        const TerminalColorScheme* kitty = TerminalColorScheme::find(StringView(u8"KITTY"));
        STD_INSIST(kitty != nullptr);
        STD_INSIST(StringView(kitty->name) == StringView(u8"kitty"));
        STD_INSIST((kitty->foregroundColor() == Color{0xdd, 0xdd, 0xdd}));
        STD_INSIST((kitty->ansiPalette()[9] == Color{0xf2, 0x20, 0x1f}));

        const TerminalColorScheme* ghostty = TerminalColorScheme::find(StringView(u8"ghostty"));
        STD_INSIST(ghostty != nullptr);
        STD_INSIST((ghostty->backgroundColor() == Color{0x28, 0x2c, 0x34}));

        const TerminalColorScheme* konsole = TerminalColorScheme::find(StringView(u8"konsole"));
        STD_INSIST(konsole != nullptr);
        STD_INSIST((konsole->ansiPalette()[12] == Color{0x3d, 0xae, 0xe9}));
    }

    STD_TEST(RejectsUnknownAndPartialNames) {
        STD_INSIST(TerminalColorScheme::find(StringView(u8"3024")) == nullptr);
        STD_INSIST(TerminalColorScheme::find(StringView(u8"no such scheme")) == nullptr);
    }
}
