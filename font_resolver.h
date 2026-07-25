/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "font.h"

namespace stl {
    class ObjPool;
}

struct FontVariants {
    FontSource regular;
    FontSource bold;
    FontSource italic;
    FontSource boldItalic;
};

FontVariants resolveFontconfig(stl::ObjPool* pool, stl::StringView fontname);
void finalizeFontconfig() noexcept;
