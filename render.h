/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/sys/types.h>

namespace stl {
    class ObjPool;
}

struct Composer;
struct TerminalCell;
struct TerminalUpdate;

namespace plt {
    struct RenderContext;
}

struct Renderer {
    virtual bool update(const TerminalUpdate& update) = 0;
    virtual bool repaint() = 0;

    // The renderer and everything it registers live in `pool`; destroying
    // the pool (composer.rendererPool for the interactive renderer) tears
    // the renderer down completely. A renderer that loses its surface
    // drops that pool itself from inside update()/repaint() and leaves
    // composer.renderer null for frame() to rebuild.
    static Renderer* create(Composer& composer, stl::ObjPool& pool, const plt::RenderContext& context);

    static u32 cellAttributes(const TerminalCell& cell);
};
