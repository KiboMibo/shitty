/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "font.h"

#include <std/str/view.h>
#include <std/sys/types.h>

namespace stl {
    class ObjPool;
}

struct Composer;

struct Fontpack {
    virtual u16 getPx() const = 0;
    virtual u16 getPy() const = 0;

    virtual bool hasBold() const = 0;
    virtual bool hasItalic() const = 0;
    virtual bool hasBoldItalic() const = 0;

    // Picks the first face whose cmap covers every codepoint of the
    // cluster (joiners and variation selectors are ignored), starting from
    // the primary family and walking the fallback list; a cluster no face
    // covers renders as a hollow box. Never returns an empty glyph.
    virtual FontGlyph glyph(const u32* codepoints, size_t count, FontStyle style, bool doubleWidth) = 0;

    // The face whose cmap covers the whole cluster, primary first then the
    // fallback walk; null when nothing covers it. The result is stable for
    // the pack's lifetime, so its FontFace ids key span caches.
    virtual Font* resolveFace(const u32* codepoints, size_t count) = 0;

    // names[0] is the primary font and defines the cell metrics; the rest
    // are fallbacks in priority order, followed by the embedded fonts.
    static Fontpack* create(Composer& composer, stl::ObjPool& pool, const stl::StringView* names, size_t nameCount, u16 size);
};
