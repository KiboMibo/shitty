/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "brand_scheme.h"

#include <math.h>

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

    // Three linear moves in rectangular Oklab, no guards:
    //   lighten: L = L0 + (1 - L0) * lighten          (1 = white)
    //   tint:    ab = chord from ab0 to the accent    (1 = accent hue)
    //   pastel:  ab = ab * (1 - pastel)               (1 = gray)
    // The tint target keeps the color's own chroma, so a cool color
    // fades through gray on its way to warm instead of visiting
    // foreign hues; a gray color has a gray target and stays gray.
    struct Oklab {
        double lightness;
        double a;
        double b;
    };

    struct Linear {
        double red;
        double green;
        double blue;
    };

    static double srgbToLinear(double byte) {
        const double c = byte / 255.0;
        if (c <= 0.04045) {
            return c / 12.92;
        }
        return pow((c + 0.055) / 1.055, 2.4);
    }

    static double linearToSrgb(double c) {
        if (c <= 0.0031308) {
            return c * 12.92 * 255.0;
        }
        return (1.055 * pow(c, 1.0 / 2.4) - 0.055) * 255.0;
    }

    static Oklab toOklab(const u8 color[3]) {
        const double r = srgbToLinear(color[0]);
        const double g = srgbToLinear(color[1]);
        const double b = srgbToLinear(color[2]);
        const double l = cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
        const double m = cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
        const double s = cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
        Oklab result;
        result.lightness = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
        result.a = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
        result.b = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
        return result;
    }

    static Linear oklabToLinearRgb(const Oklab& color) {
        double l = color.lightness + 0.3963377774 * color.a + 0.2158037573 * color.b;
        double m = color.lightness - 0.1055613458 * color.a - 0.0638541728 * color.b;
        double s = color.lightness - 0.0894841775 * color.a - 1.2914855480 * color.b;
        l = l * l * l;
        m = m * m * m;
        s = s * s * s;
        Linear result;
        result.red = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
        result.green = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
        result.blue = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;
        return result;
    }

    static bool inGamut(const Linear& rgb) {
        const double slack = 1e-9;
        if (rgb.red < -slack || rgb.red > 1.0 + slack) {
            return false;
        }
        if (rgb.green < -slack || rgb.green > 1.0 + slack) {
            return false;
        }
        return rgb.blue >= -slack && rgb.blue <= 1.0 + slack;
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

    static double clampUnit(double value) {
        if (value < 0.0) {
            return 0.0;
        }
        if (value > 1.0) {
            return 1.0;
        }
        return value;
    }

    // Out-of-gamut colors give up chroma, never lightness or hue.
    static Color oklabToColor(const Oklab& color) {
        Linear rgb = oklabToLinearRgb(color);
        if (!inGamut(rgb)) {
            double low = 0.0;
            double high = 1.0;
            for (int step = 0; step < 40; ++step) {
                const double middle = (low + high) / 2.0;
                const Oklab scaled = {color.lightness, color.a * middle, color.b * middle};
                if (inGamut(oklabToLinearRgb(scaled))) {
                    low = middle;
                } else {
                    high = middle;
                }
            }
            rgb = oklabToLinearRgb({color.lightness, color.a * low, color.b * low});
        }
        return {channel(linearToSrgb(clampUnit(rgb.red))), channel(linearToSrgb(clampUnit(rgb.green))), channel(linearToSrgb(clampUnit(rgb.blue)))};
    }
}

AnsiPalette makeBrandPalette(Color accent, double tint, double pastel, double lighten) {
    const u8 accentBytes[3] = {accent.red, accent.green, accent.blue};
    const Oklab accentLab = toOklab(accentBytes);
    const double accentChroma = sqrt(accentLab.a * accentLab.a + accentLab.b * accentLab.b);
    AnsiPalette result;
    for (size_t index = 0; index < AnsiPalette::colorCount; ++index) {
        const Oklab base = toOklab(vgaColors[index]);
        const double baseChroma = sqrt(base.a * base.a + base.b * base.b);
        const double targetA = accentLab.a / accentChroma * baseChroma;
        const double targetB = accentLab.b / accentChroma * baseChroma;
        Oklab mixed;
        mixed.lightness = base.lightness + (1.0 - base.lightness) * lighten;
        mixed.a = (base.a + (targetA - base.a) * tint) * (1.0 - pastel);
        mixed.b = (base.b + (targetB - base.b) * tint) * (1.0 - pastel);
        result[index] = oklabToColor(mixed);
    }
    return result;
}
