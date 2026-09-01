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
struct SymbolFontSpan;

// A cluster no loaded face covers, thrown out of resolveFace when the
// verdict is still unknown. The frame unwinds like a lost surface: the
// renderer catches it at the top, asks the pack to adopt a face (or to
// record that nothing serves the cluster), and re-runs the frame.
struct FontFaceMiss {
    static constexpr size_t limit = 32;

    u32 codepoints[limit];
    size_t count = 0;
};

struct Fontpack {
    virtual u16 getPx() const = 0;
    virtual u16 getPy() const = 0;
    // The regular primary face's light stem width in physical pixels. A
    // non-positive value asks Composer to use its deterministic cell-based
    // fallback (used by headless fontpacks).
    virtual float boxDrawingStroke() const = 0;

    virtual bool hasBold() const = 0;
    virtual bool hasItalic() const = 0;
    virtual bool hasBoldItalic() const = 0;

    // The face whose cmap covers the whole cluster, primary first then the
    // fallback walk; null when the cluster is known uncovered. A cluster
    // with no verdict yet throws FontFaceMiss instead. Adopted faces only
    // append, so resolved results stay stable for the pack's lifetime and
    // FontFace ids key span caches.
    virtual Font* resolveFace(const u32* codepoints, size_t count) = 0;

    // Resolves the missed cluster through the resolver chain: adopts a
    // covering face as a new fallback, or records the cluster uncovered.
    // Either way the next resolveFace for it returns without throwing.
    virtual void adoptFaceFor(const FontFaceMiss& miss) = 0;

    // The styled variant a resolved face renders with: the primary family
    // swaps to its bold/italic member (or a synthetic style), fallbacks
    // render as themselves.
    virtual Font* styledFace(Font* face, FontStyle style) const = 0;

    // names[0] is the primary font and defines the cell metrics; the rest
    // are fallbacks in priority order, followed by the embedded fonts.
    // symbols are the [[symbolFont]] config entries: inside their ranges
    // the named fonts beat the coverage walk, in entry order.
    static Fontpack* create(Composer& composer, stl::ObjPool& pool, const stl::StringView* names, size_t nameCount, const SymbolFontSpan* symbols, size_t symbolCount, u16 size);
};
