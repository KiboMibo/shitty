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
