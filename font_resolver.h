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
    // The returned face is born unreferenced and the caller adopts it into
    // an IntrusivePtr. nullptr lets the next resolver try. Views from
    // request are valid only for the duration of this call. pixels is a
    // hint: a backend whose face choice depends on optical size may honor
    // it, the face itself is size-independent.
    virtual FontFace* resolve(const FontRequest& request) = 0;
    // Enumerates the faces this resolver contributes as implicit coverage
    // fallbacks after the configured families: index counts from zero,
    // null ends the walk. Faces follow the resolve() ownership convention.
    virtual FontFace* fallback(size_t index);
};
