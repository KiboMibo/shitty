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

// One rasterized glyph mask: tightly packed 8-bit coverage rows (the pitch
// is the width) with FreeType-convention bearings - left offsets the pen
// rightward, top rises above the baseline.
struct GlyphStrike {
    const u8* data = nullptr;
    u16 width = 0;
    u16 height = 0;
    i16 left = 0;
    i16 top = 0;
};

// The composer-wide memo of rasterized glyph masks. Shaping stays with the
// font backends; this only spares the rasterizer re-inking a glyph it has
// already inked - unique text misses the span caches by construction, so
// without this memo cat throughput is bounded by the rasterizer. Keys are
// opaque: a backend packs a namespace obtained here with its native glyph
// id and whatever else selects the pixels (applied pixel size, subpixel
// phase). A strike returned by find stays valid until the next insert or
// clear.
struct GlyphCache {
    virtual u32 makeNamespace() = 0;
    virtual bool find(u64 key, GlyphStrike& strike) = 0;
    virtual void insert(u64 key, const GlyphStrike& strike) = 0;
    virtual void clear() = 0;
};

GlyphCache* createGlyphCache(stl::ObjPool& pool);
