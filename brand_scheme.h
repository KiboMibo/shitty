/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "ansi_palette.h"

// The built-in "default" color scheme: the VGA text mode palette moved
// linearly in OkLCh by the tint slider - lightness toward white, chroma
// toward gray, hue toward the accent along the shortest arc. Foreground
// and background are not part of the ramp and stay plain white on
// black.
AnsiPalette makeBrandPalette(Color accent, double tint);
