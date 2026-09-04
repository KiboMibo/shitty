/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "vt_state.h"

#include "listener.h"

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
