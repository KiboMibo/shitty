/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/lib/node.h>
#include <std/ptr/intrusive.h>
#include <std/sys/types.h>

namespace stl {
    class ObjPool;
}

struct FontFace;

struct FontGlyph {
    const void* data = nullptr;
    size_t len = 0;
    bool color = false;
};

enum class FontStyle : u8 {
    Regular,
    Bold,
    Italic,
    BoldItalic,
};

enum class FontKind : u8 {
    Primary,
    Overlay,
    Fallback,
};

struct FontMetrics {
    u16 width = 0;
    u16 height = 0;
    u16 baseline = 0;
};

// A mask occupies cells * metrics.width * metrics.height bytes. A color
// glyph occupies four times as much and contains premultiplied RGBA pixels.
// The returned bitmap remains valid until the next glyph() call on the same
// Font.
struct Font {
    virtual FontGlyph glyph(const u32* codepoints, size_t count, u16 cells) = 0;
    // Rasterizes a whole span into buf, which arrives zeroed: the flat
    // codepoint string of the span's cells, a captured blank cell as a
    // space. cells is the strip width the grid dictates — the authority
    // when the string's table width disagrees (an orphaned wide at the
    // row edge) — and sizes buf: cells slices side by side, row-major
    // with the strip width as stride, in the font's own plane and
    // metrics. A unit may ink any of its cells: a pictogram followed by
    // a space is free to use both slices.
    virtual void render(const u32* codepoints, size_t count, u16 cells, void* buf) = 0;
    // A cmap lookup: whether this face has a glyph for the codepoint.
    virtual bool covers(u32 codepoint) = 0;
    // Whether this font rasterizes color (RGBA) rather than coverage; the
    // caller picks the span plane by it.
    virtual bool colored() const = 0;
    // A new font over the same face that fakes the style (embolden/shear)
    // at render time; null when the backend cannot synthesize.
    virtual Font* synthesize(stl::ObjPool& owner, FontStyle style) = 0;
    // The size-independent face behind this font, alive as long as the
    // font: hand it to Composer::renderFace to rasterize the same face at
    // another pixel size.
    virtual FontFace* face() = 0;
};

// Whether the codepoint is a private-use pictogram: a span may hand such
// a cluster the blank cell captured after it, and the font is free to ink
// both slices.
bool puaSymbol(u32 codepoint);

// Rasterizes a resolved face at a pixel size. Any renderer accepts any
// FontFace, so the resolver and rasterizer backends combine freely;
// renderers chain like resolvers do and the first one that renders the
// face wins. For a Primary request metrics is an output; an Overlay must
// match the imposed cell geometry and a mismatch throws or returns null.
struct FontRenderer: stl::IntrusiveNode {
    virtual Font* render(stl::ObjPool& owner, stl::IntrusivePtr<FontFace> face, u16 pixels, FontKind kind, FontMetrics& metrics) = 0;
};
