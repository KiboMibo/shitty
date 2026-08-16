/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

struct Composer;
struct Renderer;

namespace plt {
    struct RenderContext;
}

namespace stl {
    class ObjPool;
}

Renderer* createMetalRenderer(Composer& composer, stl::ObjPool& pool, const plt::RenderContext& context);
