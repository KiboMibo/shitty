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

// The pixel plane a cluster asks for: an explicit variation selector
// rules, a default-emoji base wants Color, Any means no preference.
enum class FontPlane : u8 {
    Any,
    Color,
    Mask
};

struct FontResolver: stl::IntrusiveNode {
    // The returned face is born unreferenced and the caller adopts it into
    // an IntrusivePtr. nullptr lets the next resolver try. Views from
    // request are valid only for the duration of this call. pixels is a
    // hint: a backend whose face choice depends on optical size may honor
    // it, the face itself is size-independent.
    virtual FontFace* resolve(const FontRequest& request) = 0;
    // The face this resolver would serve the cluster with, queried when
    // no loaded face covers it; null lets the next resolver try. Faces
    // follow the resolve() ownership convention.
    virtual FontFace* resolveCluster(const u32* codepoints, size_t count, FontPlane plane);
};
