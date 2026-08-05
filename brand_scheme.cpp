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
    //   lighten: L = L + (1 - L) * lighten            (1 = white)
    //   tint:    chord from the color to its amber    (1 = accent hue)
    //   pastel:  ab = ab * (1 - pastel)               (1 = gray)
    // The tint target wants the slot's own lightness and chroma at the
    // accent hue. Where no such color exists in sRGB - a vivid amber
    // cannot be as light as vivid yellow - the target is pulled toward
    // the accent hue's cusp, trading lightness for staying vivid,
    // instead of bleaching toward white.
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

namespace {
    static double maxChromaAt(double lightness, double unitA, double unitB) {
        double low = 0.0;
        double high = 0.5;
        for (int probe = 0; probe < 30; ++probe) {
            const double middle = (low + high) / 2.0;
            if (inGamut(oklabToLinearRgb({lightness, unitA * middle, unitB * middle}))) {
                low = middle;
            } else {
                high = middle;
            }
        }
        return low;
    }

    // The most chromatic representable color of a hue; the corner of
    // the sRGB gamut slice. Chroma over lightness is unimodal, so a
    // ternary search finds the corner.
    static Oklab hueCusp(double unitA, double unitB) {
        double low = 0.01;
        double high = 0.99;
        for (int step = 0; step < 60; ++step) {
            const double left = low + (high - low) / 3.0;
            const double right = high - (high - low) / 3.0;
            if (maxChromaAt(left, unitA, unitB) < maxChromaAt(right, unitA, unitB)) {
                low = left;
            } else {
                high = right;
            }
        }
        const double lightness = (low + high) / 2.0;
        const double chroma = maxChromaAt(lightness, unitA, unitB) * 0.999;
        Oklab result;
        result.lightness = lightness;
        result.a = unitA * chroma;
        result.b = unitB * chroma;
        return result;
    }

    // Where a slot heads at full tint: the accent hue, the slot's own
    // lightness pulled halfway to the hue's cusp (vivid amber simply
    // does not exist at vivid yellow's lightness), and as much of the
    // slot's chroma as the slice can hold there. Lightness rank is
    // preserved, so the sixteen targets stay sixteen colors.
    static Oklab tintTarget(const Oklab& base, const Oklab& cusp, double unitA, double unitB) {
        const double baseChroma = sqrt(base.a * base.a + base.b * base.b);
        if (baseChroma < 1e-6) {
            return base;
        }
        const double lightness = base.lightness + (cusp.lightness - base.lightness) * 0.5;
        double chroma = maxChromaAt(lightness, unitA, unitB) * 0.999;
        if (baseChroma < chroma) {
            chroma = baseChroma;
        }
        Oklab result;
        result.lightness = lightness;
        result.a = unitA * chroma;
        result.b = unitB * chroma;
        return result;
    }
}

AnsiPalette makeBrandPalette(Color accent, double tint, double pastel, double lighten) {
    const u8 accentBytes[3] = {accent.red, accent.green, accent.blue};
    const Oklab accentLab = toOklab(accentBytes);
    const double accentChroma = sqrt(accentLab.a * accentLab.a + accentLab.b * accentLab.b);
    const double unitA = accentLab.a / accentChroma;
    const double unitB = accentLab.b / accentChroma;
    const Oklab cusp = hueCusp(unitA, unitB);
    AnsiPalette result;
    for (size_t index = 0; index < AnsiPalette::colorCount; ++index) {
        const Oklab base = toOklab(vgaColors[index]);
        const Oklab target = tintTarget(base, cusp, unitA, unitB);
        Oklab mixed;
        mixed.lightness = base.lightness + (target.lightness - base.lightness) * tint;
        mixed.lightness = mixed.lightness + (1.0 - mixed.lightness) * lighten;
        mixed.a = (base.a + (target.a - base.a) * tint) * (1.0 - pastel);
        mixed.b = (base.b + (target.b - base.b) * tint) * (1.0 - pastel);
        result[index] = oklabToColor(mixed);
    }
    return result;
}
