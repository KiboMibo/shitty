/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "color_spec.h"

#include <std/tst/ut.h>

using namespace stl;

namespace {
    bool close(u8 value, u8 expected, u8 tolerance = 1) {
        return value >= expected - tolerance && value <= expected + tolerance;
    }
}

STD_TEST_SUITE(ColorSpec) {
    STD_TEST(ParsesHashWidthsByHighBits) {
        Color color{};

        STD_INSIST(parseXColor("#abc", color));
        STD_INSIST((color == Color{0xa0, 0xb0, 0xc0}));

        STD_INSIST(parseXColor("#123456", color));
        STD_INSIST((color == Color{0x12, 0x34, 0x56}));

        STD_INSIST(parseXColor("#123456789", color));
        STD_INSIST((color == Color{0x12, 0x45, 0x78}));

        STD_INSIST(parseXColor("#1234abcdffff", color));
        STD_INSIST((color == Color{0x12, 0xab, 0xff}));
    }

    STD_TEST(ParsesRgbComponentsByTheirOwnWidths) {
        Color color{};

        STD_INSIST(parseXColor("rgb:f/80/ffff", color));
        STD_INSIST((color == Color{255, 128, 255}));
        STD_INSIST(parseXColor("RGB:0/7f/ff", color));
        STD_INSIST((color == Color{0, 127, 255}));
    }

    STD_TEST(RejectsMalformedHexForms) {
        Color color{};

        STD_INSIST(!parseXColor("#12", color));
        STD_INSIST(!parseXColor("#12345", color));
        STD_INSIST(!parseXColor("#ggg", color));
        STD_INSIST(!parseXColor("rgb:/0/0", color));
        STD_INSIST(!parseXColor("rgb:0/0/0/0", color));
        STD_INSIST(!parseXColor("rgb:00000/0/0", color));
    }

    STD_TEST(ParsesLinearRgbIntensity) {
        Color color{};

        STD_INSIST(parseXColor("rgbi:0/0.5/1", color));
        STD_INSIST(color.red == 0);
        STD_INSIST(close(color.green, 188));
        STD_INSIST(color.blue == 255);
    }

    STD_TEST(ParsesCaseInsensitiveConvertedColorNames) {
        Color lower{};
        Color upper{};

        STD_INSIST(parseXColor("ciexyz:0.4124564/0.2126729/0.0193339", lower));
        STD_INSIST(parseXColor("CIEXYZ:0.4124564/0.2126729/0.0193339", upper));
        STD_INSIST(lower == upper);
        STD_INSIST(close(lower.red, 255));
        STD_INSIST(close(lower.green, 0));
        STD_INSIST(close(lower.blue, 0));
    }

    STD_TEST(ParsesLabWhiteAndBlack) {
        Color color{};

        STD_INSIST(parseXColor("CIELab:100/0/0", color));
        STD_INSIST(close(color.red, 255));
        STD_INSIST(close(color.green, 255));
        STD_INSIST(close(color.blue, 255));

        STD_INSIST(parseXColor("CIELab:0/0/0", color));
        STD_INSIST((color == Color{0, 0, 0}));
    }

    STD_TEST(ParsesScientificNotation) {
        Color decimal{};
        Color scientific{};

        STD_INSIST(parseXColor("rgbi:0.25/0.5/0.75", decimal));
        STD_INSIST(parseXColor("rgbi:2.5e-1/5E-1/7.5e-1", scientific));
        STD_INSIST(decimal == scientific);
    }

    STD_TEST(RejectsInvalidConvertedRangesAndNumbers) {
        Color color{};

        STD_INSIST(!parseXColor("rgbi:-0.1/0/0", color));
        STD_INSIST(!parseXColor("rgbi:0/0/1.1", color));
        STD_INSIST(!parseXColor("CIEXYZ:0/1.1/0", color));
        STD_INSIST(!parseXColor("CIELab:101/0/0", color));
        STD_INSIST(!parseXColor("TekHVC:0/50/-1", color));
        STD_INSIST(!parseXColor("rgbi:1e/0/0", color));
        STD_INSIST(!parseXColor("unknown:0/0/0", color));
    }

    STD_TEST(GamutMapsOutOfRangeChromaticity) {
        Color color{};

        STD_INSIST(parseXColor("CIEXYZ:2/0.5/-1", color));
        STD_INSIST(!(color == Color{0, 0, 0}));
    }
}
