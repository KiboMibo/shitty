/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/lib/list.h>
#include <std/str/view.h>
#include <std/sys/types.h>

namespace stl {
    class ObjPool;
    class SmallObjAllocator;
}

namespace plt {
    struct Platform;
    struct Window;
}

struct CellExtraStore;
struct VtConfig;

// The complete embedding surface of the VT core: the configuration and
// services the terminal state machine reads, and the grid geometry it
// serves back. The embedder owns one - in the shitty binaries Composer
// embeds it and the GUI wiring fills it - and commits state here before
// walking the listener lists.
struct VtState {
    void setGlyphSize(u16 width, u16 height);
    void setCellExtras(CellExtraStore* extras);
    // resize() commits all geometry fields before walking
    // resizedListeners.
    void resize(u16 pixelWidth, u16 pixelHeight);
    u16 borderPixels() const;

    const VtConfig* config = nullptr;
    // Process-lifetime allocations of the core: terminals park their
    // fiber proxies and session state here.
    stl::ObjPool* pool = nullptr;
    CellExtraStore* cellExtras = nullptr;
    stl::SmallObjAllocator* smallObjects = nullptr;
    plt::Platform* platform = nullptr;
    plt::Window* window = nullptr;
    // The product name the terminal reports (XTVERSION) and prefixes its
    // diagnostics with.
    stl::StringView brandName;

    u16 columns = 0;
    u16 rows = 0;
    u16 pixelWidth = 0;
    u16 pixelHeight = 0;
    u16 glyphWidth = 0;
    u16 glyphHeight = 0;
    // The unscaled border the embedder configured; borderPixels() scales
    // it by the content scale.
    u16 baseBorder = 0;
    float contentScale = 1.0f;

    stl::IntrusiveList resizedListeners;
    stl::IntrusiveList fontChangedListeners;
    stl::IntrusiveList cellExtrasChangedListeners;
    // Vterms publish their own undecorated title here. The session owner
    // decides whether the source is visible and how the window presents
    // it.
    stl::IntrusiveList titleChangedListeners;
    stl::IntrusiveList configChangedListeners;
};
