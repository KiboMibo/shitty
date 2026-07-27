/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "font.h"

#include <std/lib/node.h>
#include <std/str/view.h>

namespace stl {
    class ObjPool;
}

struct FontRequest {
    stl::StringView name;
    u16 pixels = 0;
    FontStyle style = FontStyle::Regular;
    FontKind kind = FontKind::Primary;
};

struct FontResolver: stl::IntrusiveNode {
    // The returned font belongs to owner. nullptr lets the next resolver try.
    // Views from request are valid only for the duration of this call.
    virtual Font* load(stl::ObjPool& owner, const FontRequest& request, FontMetrics& metrics) = 0;
};
