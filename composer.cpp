/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "composer.h"

#include "listener.h"
#include "options.h"

#include <std/alg/minmax.h>

#include <cassert>

using namespace stl;

Composer::Composer(ObjPool* pool_)
    : pool(pool_)
{
}

void Composer::setGlyphSize(u16 width, u16 height) {
    assert(width != 0);
    assert(height != 0);
    glyphWidth = width;
    glyphHeight = height;
}

void Composer::resize(u16 pixelWidth_, u16 pixelHeight_) {
    assert(glyphWidth != 0);
    assert(glyphHeight != 0);

    const u32 border = 2u * opts.border;
    const u32 contentWidth = pixelWidth_ > border ? pixelWidth_ - border : 0;
    const u32 contentHeight = pixelHeight_ > border ? pixelHeight_ - border : 0;
    const u16 columns_ = (u16)(max<u32>(1, contentWidth / glyphWidth));
    const u16 rows_ = (u16)(max<u32>(1, contentHeight / glyphHeight));

    if (columns == columns_ && rows == rows_ && pixelWidth == pixelWidth_ && pixelHeight == pixelHeight_) {
        return;
    }

    columns = columns_;
    rows = rows_;
    pixelWidth = pixelWidth_;
    pixelHeight = pixelHeight_;

    for (IntrusiveNode* node = resizedListeners.mutFront(); node != resizedListeners.mutEnd();) {
        Listener* const listener = static_cast<Listener*>(node);
        node = node->next;
        listener->onListen();
    }
}
