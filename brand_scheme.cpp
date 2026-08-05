/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "brand_scheme.h"

namespace {
    // The sixteen colors of the VGA text mode palette.
    constexpr u8 vgaColors[AnsiPalette::colorCount][3] = {
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

    // How far the full-tint endpoint lifts each color toward white; the
    // "pastel" half of the look, scaled by the same slider as the hue.
    constexpr double pastelLift = 0.30;

    // Default foreground and background sit outside the palette ramp:
    // the background is a near-black shade of the accent, the foreground
    // a cream just below white.
    constexpr double backgroundLuma = 18.0;
    constexpr double foregroundLuma = 235.0;

    static double lumaOf(double red, double green, double blue) {
        return red * 0.299 + green * 0.587 + blue * 0.114;
    }

    static u8 channel(double value) {
        if (value <= 0.0) {
            return 0;
        }
        if (value >= 255.0) {
            return 255;
        }
        return (u8)(value + 0.5);
    }

    static Color mixColors(Color from, Color to, double amount) {
        Color result;
        result.red = channel(from.red + (to.red - (double)(from.red)) * amount);
        result.green = channel(from.green + (to.green - (double)(from.green)) * amount);
        result.blue = channel(from.blue + (to.blue - (double)(from.blue)) * amount);
        return result;
    }

    // The accent stretched into a full luma ramp: scaled down below its
    // own luma, blended toward white above it, so hue survives darkening
    // and lightening alike.
    static Color accentAtLuma(Color accent, double luma) {
        const double accentLuma = lumaOf(accent.red, accent.green, accent.blue);
        if (luma <= accentLuma) {
            const double scale = luma / accentLuma;
            return {channel(accent.red * scale), channel(accent.green * scale), channel(accent.blue * scale)};
        }
        const double toWhite = (luma - accentLuma) / (255.0 - accentLuma);
        return mixColors(accent, {255, 255, 255}, toWhite);
    }
}

BrandScheme makeBrandScheme(Color accent, double tint) {
    BrandScheme result;
    for (size_t index = 0; index < AnsiPalette::colorCount; ++index) {
        const Color base = {vgaColors[index][0], vgaColors[index][1], vgaColors[index][2]};
        const double luma = lumaOf(base.red, base.green, base.blue);
        const double lifted = luma + (255.0 - luma) * pastelLift;
        result.palette[index] = mixColors(base, accentAtLuma(accent, lifted), tint);
    }
    result.background = mixColors({0, 0, 0}, accentAtLuma(accent, backgroundLuma), tint);
    result.foreground = mixColors({255, 255, 255}, accentAtLuma(accent, foregroundLuma), tint);
    return result;
}
