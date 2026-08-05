/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "ansi_palette.h"

// The built-in "default" color scheme: the VGA text mode palette pulled
// toward the brand accent. The tint slider blends every color from the
// plain VGA value at 0 to a pastel sepia ramp of the accent at 1 - hue
// from the accent, luma lifted toward white - so the brand look is one
// number away from the stock look.
struct BrandScheme {
    Color foreground;
    Color background;
    AnsiPalette palette;
};

BrandScheme makeBrandScheme(Color accent, double tint);
