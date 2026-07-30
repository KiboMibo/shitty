/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "font.h"

#include <std/str/view.h>

namespace stl {
    class ObjPool;
}

Font* createFreeTypeFont(stl::ObjPool& owner, stl::StringView filename, i32 faceIndex, u16 pixels, FontKind kind, FontMetrics& metrics);

// The data must stay valid for the lifetime of the font.
Font* createFreeTypeMemoryFont(stl::ObjPool& owner, const void* data, size_t size, i32 faceIndex, u16 pixels, FontKind kind, FontMetrics& metrics);
