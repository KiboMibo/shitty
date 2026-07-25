/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/str/view.h>
#include <std/sys/types.h>

namespace stl {
    class ObjPool;
}

struct FontSource {
    stl::StringView filename;
    i32 index = 0;
};

struct FontGlyph {
    const void* data = nullptr;
    size_t len = 0;
    bool color = false;
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

// A mask occupies metrics.width * metrics.height bytes. A color glyph occupies
// four times as much and contains premultiplied RGBA pixels. The returned
// bitmap remains valid until the next glyph() call on the same Font.
struct Font {
    virtual FontGlyph glyph(const u32* codepoints, size_t count) = 0;

    static Font* create(stl::ObjPool& pool, FontSource source, u16 size, FontKind kind, FontMetrics& metrics);
};
