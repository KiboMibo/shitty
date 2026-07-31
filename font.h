/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/sys/types.h>

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
    // A cmap lookup: whether this face has a glyph for the codepoint.
    virtual bool covers(u32 codepoint) = 0;
};
