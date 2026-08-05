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

    constexpr u8 kitty[AnsiPalette::colorCount][3] = {
        {0x00, 0x00, 0x00},
        {0xcc, 0x04, 0x03},
        {0x19, 0xcb, 0x00},
        {0xce, 0xcb, 0x00},
        {0x0d, 0x73, 0xcc},
        {0xcb, 0x1e, 0xd1},
        {0x0d, 0xcd, 0xcd},
        {0xdd, 0xdd, 0xdd},
        {0x76, 0x76, 0x76},
        {0xf2, 0x20, 0x1f},
        {0x23, 0xfd, 0x00},
        {0xff, 0xfd, 0x00},
        {0x1a, 0x8f, 0xff},
        {0xfd, 0x28, 0xff},
        {0x14, 0xff, 0xff},
        {0xff, 0xff, 0xff},
    };
}

STD_TEST_SUITE(BrandScheme) {
    STD_TEST(TintZeroIsExactBase) {
        const AnsiPalette palette = makeBrandPalette(amber, 0.0, 0.0, 0.0);

        for (size_t index = 0; index < AnsiPalette::colorCount; ++index) {
            STD_INSIST((palette[index] == Color{kitty[index][0], kitty[index][1], kitty[index][2]}));
        }
    }

    STD_TEST(TintedColorsStayDistinct) {
        const AnsiPalette palette = makeBrandPalette(amber, 1.0, 0.15, 0.05);

        for (size_t left = 0; left < AnsiPalette::colorCount; ++left) {
            for (size_t right = left + 1; right < AnsiPalette::colorCount; ++right) {
                STD_INSIST(!(palette[left] == palette[right]));
            }
        }
    }

    STD_TEST(BrightsStayBrighterThanNormals) {
        const AnsiPalette palette = makeBrandPalette(amber, 0.25, 0.15, 0.05);

        for (size_t index = 1; index < 7; ++index) {
            const Color normal = palette[index];
            const Color bright = palette[index + 8];
            const int normalLuma = normal.red * 299 + normal.green * 587 + normal.blue * 114;
            const int brightLuma = bright.red * 299 + bright.green * 587 + bright.blue * 114;

            STD_INSIST(brightLuma > normalLuma);
        }
    }
}
