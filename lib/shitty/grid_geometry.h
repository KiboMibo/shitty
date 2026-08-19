/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "composer.h"

#include <std/alg/minmax.h>

// A1: the two directions of the same formula - how many cells a surface
// holds once its content insets are taken out, and how many pixels a
// grid of that many cells needs with those insets put back. Every place
// that used to spell `2 * borderPixels()` around a grid goes through
// these, so a per-side inset cannot be dropped, doubled, or applied to
// the wrong axis in one caller and not the next.
//
// The insets are per side (see Insets in composer.h) and in backing
// pixels, so the width pair reads `left`/`right` and the height pair
// reads `top`/`bottom` - never `2 *` one of them.

inline u32 gridColumns(u32 pixelWidth, const Insets& insets, u16 glyphWidth) {
    const u32 reserved = (u32)(insets.left) + insets.right;
    const u32 content = pixelWidth > reserved ? pixelWidth - reserved : 0;
    return stl::max<u32>(1, content / stl::max<u32>(1, glyphWidth));
}

inline u32 gridRows(u32 pixelHeight, const Insets& insets, u16 glyphHeight) {
    const u32 reserved = (u32)(insets.top) + insets.bottom;
    const u32 content = pixelHeight > reserved ? pixelHeight - reserved : 0;
    return stl::max<u32>(1, content / stl::max<u32>(1, glyphHeight));
}

// The inverse. `columns` of zero asks for just the reserve, which is
// what a resize increment or a minimum-size base wants.
inline u32 gridPixelWidth(u32 columns, const Insets& insets, u16 glyphWidth) {
    return (u32)(insets.left) + insets.right + columns * glyphWidth;
}

inline u32 gridPixelHeight(u32 rows, const Insets& insets, u16 glyphHeight) {
    return (u32)(insets.top) + insets.bottom + rows * glyphHeight;
}
