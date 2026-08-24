/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "vt_state.h"

#include "listener.h"

#include <std/alg/minmax.h>
#include <std/dbg/assert.h>

using namespace stl;

namespace {
    static void walk(IntrusiveList& listeners) {
        for (IntrusiveNode* node = listeners.mutFront(); node != listeners.mutEnd();) {
            Listener* const listener = static_cast<Listener*>(node);
            node = node->next;
            listener->onListen();
        }
    }
}

void VtState::setGlyphSize(u16 width, u16 height) {
    STD_ASSERT(width != 0);
    STD_ASSERT(height != 0);
    if (glyphWidth == width && glyphHeight == height) {
        return;
    }
    glyphWidth = width;
    glyphHeight = height;
}

void VtState::setCellExtras(CellExtraStore* extras) {
    if (cellExtras == extras) {
        return;
    }
    cellExtras = extras;
    walk(cellExtrasChangedListeners);
}

u16 VtState::borderPixels() const {
    const float scaled = baseBorder * contentScale;
    if (!(scaled > 0)) {
        return 0;
    }
    if (scaled >= 3000) {
        return 3000;
    }
    return (u16)(scaled + 0.5f);
}

void VtState::resize(u16 pixelWidth_, u16 pixelHeight_) {
    STD_ASSERT(glyphWidth != 0);
    STD_ASSERT(glyphHeight != 0);

    const u32 borders = 2u * borderPixels();
    const u32 contentWidth = pixelWidth_ > borders ? pixelWidth_ - borders : 0;
    const u32 contentHeight = pixelHeight_ > borders ? pixelHeight_ - borders : 0;
    const u16 columns_ = (u16)(max<u32>(1, contentWidth / glyphWidth));
    const u16 rows_ = (u16)(max<u32>(1, contentHeight / glyphHeight));

    if (columns == columns_ && rows == rows_ && pixelWidth == pixelWidth_ && pixelHeight == pixelHeight_) {
        return;
    }

    columns = columns_;
    rows = rows_;
    pixelWidth = pixelWidth_;
    pixelHeight = pixelHeight_;

    walk(resizedListeners);
}
