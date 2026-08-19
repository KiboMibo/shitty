/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

// PaneUpdate and PixelRect, the frame contract A2 fixed there.
#include "composer.h"

#include <std/sys/types.h>

namespace stl {
    class Buffer;
    class ObjPool;
}

struct TerminalCell;
struct TerminalUpdate;

namespace plt {
    struct RenderContext;
}

// A2: the pane-less update(), spelled out. One terminal fills the
// surface, so its pane is the surface - the whole of it, padding
// included, because the padding around a pane's grid is that pane's to
// clear (see the area note on update() below).
inline PaneUpdate surfacePane(const Composer& composer, const TerminalUpdate& update) {
    return PaneUpdate{PixelRect{0, 0, composer.pixelWidth, composer.pixelHeight}, update};
}

struct Renderer {
    // A2: the frame. One call, one frame, one present - every pane the
    // window shows contributes one PaneUpdate to the same call, and
    // there is no begin/end sequence anyone can leave unbalanced.
    // `panes` is a contiguous run of `count` entries: a pointer and a
    // count, this codebase's read-only span (see PaneUpdate in
    // composer.h for why it is not a Span type).
    //
    // Panes draw in the order given. A pane owns its `area` and nothing
    // else: the backend clears that rectangle, puts the pane's grid
    // inside it at the content insets, and clips its drawing to it. The
    // clear is per pane and not per frame, which is what lets two panes
    // with two different backgrounds share one drawable.
    //
    // The default below forwards a one-pane frame to the pane-less form
    // and refuses a wider one: a renderer that only knows a single
    // terminal (the mirror in test_mode.cpp) then stays correct instead
    // of silently dropping every pane but the first.
    virtual bool update(const PaneUpdate* panes, size_t count) {
        return count == 1 && update(panes[0].update);
    }

    // The pane-less form: one terminal, the whole surface. A thin
    // wrapper over surfacePane() in each backend, kept because the
    // headless harness, the Vulkan shadow mirror and every renderer
    // test speak in terminals rather than in panes.
    virtual bool update(const TerminalUpdate& update) = 0;
    virtual bool repaint() = 0;
    // The last presented frame as tightly packed RGB rows, for parity
    // tests against the reference renderer; false when the backend
    // cannot read its output back.
    virtual bool captureOutput(stl::Buffer& rgb, u32& width, u32& height);

    // The renderer and everything it registers live in `pool`; destroying
    // the pool (composer.rendererPool for the interactive renderer) tears
    // the renderer down completely. A renderer that loses its surface
    // drops that pool itself from inside update()/repaint() and leaves
    // composer.renderer null for frame() to rebuild.
    static Renderer* create(Composer& composer, stl::ObjPool& pool, const plt::RenderContext& context);

    static u32 cellAttributes(const TerminalCell& cell);
};
