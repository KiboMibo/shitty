/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "vt_geometry.h"

#include "vt_host.h"

#include <std/alg/minmax.h>
#include <std/dbg/assert.h>

using namespace stl;

void VtGeometry::setCellPixelSize(u16 width, u16 height) {
    STD_ASSERT(width != 0);
    STD_ASSERT(height != 0);
    if (cellPixelWidth == width && cellPixelHeight == height) {
        return;
    }
    cellPixelWidth = width;
    cellPixelHeight = height;
}

void VtGeometry::resize(u16 pixelWidth_, u16 pixelHeight_, VtHost* host) {
    STD_ASSERT(cellPixelWidth != 0);
    STD_ASSERT(cellPixelHeight != 0);

    // A1: per side, never `2 *` one of them. The pane resize() serves is
    // the one that fills the window, so its rectangle is the surface and
    // the only thing between the two is this pane's own border.
    const u32 horizontal = (u32)(insets.left) + insets.right;
    const u32 vertical = (u32)(insets.top) + insets.bottom;
    const u32 contentWidth = pixelWidth_ > horizontal ? pixelWidth_ - horizontal : 0;
    const u32 contentHeight = pixelHeight_ > vertical ? pixelHeight_ - vertical : 0;
    const u16 columns_ = (u16)(max<u32>(1, contentWidth / cellPixelWidth));
    const u16 rows_ = (u16)(max<u32>(1, contentHeight / cellPixelHeight));

    if (columns == columns_ && rows == rows_ && pixelWidth == pixelWidth_ && pixelHeight == pixelHeight_) {
        return;
    }

    columns = columns_;
    rows = rows_;
    pixelWidth = pixelWidth_;
    pixelHeight = pixelHeight_;
    // The extent the pointer mappings clamp against, committed beside
    // the grid it was divided into rather than recovered from it: the
    // division just threw away whatever did not fill a whole cell.
    width = (i32)(contentWidth);
    height = (i32)(contentHeight);

    if (host != nullptr) {
        host->resized();
    }
}
