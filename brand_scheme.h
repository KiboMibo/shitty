/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "ansi_palette.h"

// The built-in "default" color scheme: the VGA text mode palette pulled
// toward the brand accent in OkLCh. The tint slider softens every color
// toward a pastel target (lifted lightness, muted chroma) and leans its
// hue toward the accent, capped at 30 degrees so every hue keeps its
// identity. Neutrals only pick up a warm cast; foreground and
// background are not part of the ramp and stay plain white on black.
AnsiPalette makeBrandPalette(Color accent, double tint);
