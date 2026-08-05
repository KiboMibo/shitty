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

    // Where each slot heads as the slider grows: pastel lightness and
    // muted chroma in OkLCh. The neutral slots keep only a warm cast.
    // The lightness weight throttles the pastel lift: normal colors
    // double as backgrounds (mc panels, dialogs), so they keep their
    // depth and their own lightness spread; brights are text and take
    // the full lift, which widens the normal/bright separation.
    struct SoftTarget {
        double lightness;
        double lightnessWeight;
        double chroma;
        bool neutral;
    };

    constexpr SoftTarget softTargets[AnsiPalette::colorCount] = {
        {0.20, 1.0, 0.015, true},
        {0.56, 0.5, 0.110, false},
        {0.56, 0.5, 0.110, false},
        {0.56, 0.5, 0.110, false},
        {0.56, 0.5, 0.110, false},
        {0.56, 0.5, 0.110, false},
        {0.56, 0.5, 0.110, false},
        {0.78, 1.0, 0.030, true},
        {0.52, 1.0, 0.025, true},
        {0.80, 1.0, 0.100, false},
        {0.80, 1.0, 0.100, false},
        {0.80, 1.0, 0.100, false},
        {0.80, 1.0, 0.100, false},
        {0.80, 1.0, 0.100, false},
        {0.80, 1.0, 0.100, false},
        {0.97, 1.0, 0.015, true},
    };

    // A hue never travels more than this toward the accent, so blue
    // stays blue instead of collapsing into gray-brown.
    constexpr double maxLeanDegrees = 30.0;

    struct Oklch {
        double lightness;
        double chroma;
        double hue;
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

    static Oklch toOklch(const u8 color[3]) {
        const double r = srgbToLinear(color[0]);
        const double g = srgbToLinear(color[1]);
        const double b = srgbToLinear(color[2]);
        const double l = cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
        const double m = cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
        const double s = cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
        const double a = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
        const double bb = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
        Oklch result;
        result.lightness = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
        result.chroma = sqrt(a * a + bb * bb);
        result.hue = fmod(atan2(bb, a) * 180.0 / M_PI + 360.0, 360.0);
        return result;
    }

    static Linear oklchToLinear(double lightness, double chroma, double hueRadians) {
        const double a = chroma * cos(hueRadians);
        const double bb = chroma * sin(hueRadians);
        double l = lightness + 0.3963377774 * a + 0.2158037573 * bb;
        double m = lightness - 0.1055613458 * a - 0.0638541728 * bb;
        double s = lightness - 0.0894841775 * a - 1.2914855480 * bb;
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
    static Color oklchToColor(const Oklch& color) {
        const double hueRadians = color.hue * M_PI / 180.0;
        Linear rgb = oklchToLinear(color.lightness, color.chroma, hueRadians);
        if (!inGamut(rgb)) {
            double low = 0.0;
            double high = color.chroma;
            for (int step = 0; step < 40; ++step) {
                const double middle = (low + high) / 2.0;
                if (inGamut(oklchToLinear(color.lightness, middle, hueRadians))) {
                    low = middle;
                } else {
                    high = middle;
                }
            }
            rgb = oklchToLinear(color.lightness, low, hueRadians);
        }
        return {channel(linearToSrgb(clampUnit(rgb.red))), channel(linearToSrgb(clampUnit(rgb.green))), channel(linearToSrgb(clampUnit(rgb.blue)))};
    }
}

AnsiPalette makeBrandPalette(Color accent, double tint) {
    const u8 accentBytes[3] = {accent.red, accent.green, accent.blue};
    const Oklch accentLch = toOklch(accentBytes);
    // Softness leads the hue: sqrt reaches pastel well before the lean
    // toward the accent is complete.
    const double soften = sqrt(tint);
    AnsiPalette result;
    for (size_t index = 0; index < AnsiPalette::colorCount; ++index) {
        const Oklch base = toOklch(vgaColors[index]);
        const SoftTarget& target = softTargets[index];
        Oklch mixed;
        mixed.lightness = base.lightness + (target.lightness - base.lightness) * soften * target.lightnessWeight;
        if (target.neutral) {
            mixed.hue = accentLch.hue;
            mixed.chroma = base.chroma + (target.chroma - base.chroma) * soften * tint;
        } else {
            double lean = fmod(accentLch.hue - base.hue + 540.0, 360.0) - 180.0;
            if (lean > maxLeanDegrees) {
                lean = maxLeanDegrees;
            }
            if (lean < -maxLeanDegrees) {
                lean = -maxLeanDegrees;
            }
            mixed.hue = base.hue + lean * tint;
            mixed.chroma = base.chroma + (target.chroma - base.chroma) * soften;
        }
        result[index] = oklchToColor(mixed);
    }
    return result;
}
