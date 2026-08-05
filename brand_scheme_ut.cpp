/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "brand_scheme.h"

#include <std/tst/ut.h>

using namespace stl;

namespace {
    constexpr Color amber = {0xff, 0xb0, 0x00};

    constexpr u8 vga[AnsiPalette::colorCount][3] = {
        {0x00, 0x00, 0x00},
        {0xaa, 0x00, 0x00},
        {0x00, 0xaa, 0x00},
        {0xaa, 0x55, 0x00},
        {0x00, 0x00, 0xaa},
        {0xaa, 0x00, 0xaa},
        {0x00, 0xaa, 0xaa},
        {0xaa, 0xaa, 0xaa},
        {0x55, 0x55, 0x55},
        {0xff, 0x55, 0x55},
        {0x55, 0xff, 0x55},
        {0xff, 0xff, 0x55},
        {0x55, 0x55, 0xff},
        {0xff, 0x55, 0xff},
        {0x55, 0xff, 0xff},
        {0xff, 0xff, 0xff},
    };
}

STD_TEST_SUITE(BrandScheme) {
    STD_TEST(TintZeroIsExactVga) {
        const AnsiPalette palette = makeBrandPalette(amber, 0.0);

        for (size_t index = 0; index < AnsiPalette::colorCount; ++index) {
            STD_INSIST((palette[index] == Color{vga[index][0], vga[index][1], vga[index][2]}));
        }
    }

    STD_TEST(TintedColorsStayDistinct) {
        const AnsiPalette palette = makeBrandPalette(amber, 1.0);

        for (size_t left = 0; left < AnsiPalette::colorCount; ++left) {
            for (size_t right = left + 1; right < AnsiPalette::colorCount; ++right) {
                STD_INSIST(!(palette[left] == palette[right]));
            }
        }
    }

    STD_TEST(BrightsStayBrighterThanNormals) {
        const AnsiPalette palette = makeBrandPalette(amber, 0.35);

        for (size_t index = 1; index < 7; ++index) {
            const Color normal = palette[index];
            const Color bright = palette[index + 8];
            const int normalLuma = normal.red * 299 + normal.green * 587 + normal.blue * 114;
            const int brightLuma = bright.red * 299 + bright.green * 587 + bright.blue * 114;

            STD_INSIST(brightLuma > normalLuma);
        }
    }
}
