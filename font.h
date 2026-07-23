/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/sys/types.h>

namespace stl {
    class StringView;
}

struct Composer;

struct FontGlyph {
    const void* data;
    size_t len;
};

enum class FontKind : u8 {
    Primary,
    Overlay,
    DoubleWidth,
};

struct FontMetrics {
    u16 width = 0;
    u16 height = 0;
    u16 baseline = 0;
};

// The returned bitmap occupies exactly metrics.width * metrics.height bytes.
// It remains valid until the next glyph() call on the same Font.
struct Font {
    virtual FontGlyph glyph(u32 id) = 0;

    static Font* create(Composer& composer, stl::StringView filename, FontKind kind, FontMetrics& metrics);
};
