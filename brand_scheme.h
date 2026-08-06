/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "ansi_palette.h"

// The built-in "default" color scheme: the VGA text mode palette moved
// by three independent sliders, each a linear move in rectangular
// Oklab and each reaching its extreme at 1. tint drags every hue along
// a chord toward the accent, pastel scales chroma down to none,
// lighten raises lightness all the way to white. Foreground and
// background are not part of the ramp and stay plain white on black.
AnsiPalette makeBrandPalette(Color accent, double tint, double pastel, double lighten);
