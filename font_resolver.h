/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/str/view.h>

namespace stl {
    class ObjPool;
}

struct FontVariants {
    stl::StringView regular;
    stl::StringView bold;
    stl::StringView italic;
    stl::StringView boldItalic;
};

FontVariants resolveFontconfig(stl::ObjPool* pool, stl::StringView fontname);
void finalizeFontconfig() noexcept;
