/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "font.h"

#include <std/lib/node.h>
#include <std/ptr/intrusive.h>

namespace stl {
    class ObjPool;
}

// Rasterizes a resolved face at a pixel size. Any renderer accepts any
// FontFace, so the resolver and rasterizer backends combine freely;
// renderers chain like resolvers do and the first one that renders the
// face wins. For a Primary request metrics is an output; an Overlay must
// match the imposed cell geometry and a mismatch throws or returns null.
struct FontRenderer: stl::IntrusiveNode {
    virtual Font* render(stl::ObjPool& owner, stl::IntrusivePtr<FontFace> face, u16 pixels, FontKind kind, FontMetrics& metrics) = 0;
};
