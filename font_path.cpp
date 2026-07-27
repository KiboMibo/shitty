/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "font_path.h"

#include "composer.h"
#include "font_freetype.h"
#include "font_resolver.h"

#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    struct PathFontResolverImpl final: public FontResolver {
        explicit PathFontResolverImpl(Composer& composer);

        Font* load(ObjPool& owner, const FontRequest& request, FontMetrics& metrics) override;
    };
}

PathFontResolverImpl::PathFontResolverImpl(Composer& composer) {
    composer.fontResolvers.pushBack(this);
}

Font* PathFontResolverImpl::load(ObjPool& owner, const FontRequest& request, FontMetrics& metrics) {
    if (request.style != FontStyle::Regular || (!request.name.memChr('/') && !request.name.memChr('\\'))) {
        return nullptr;
    }
    return createFreeTypeFont(owner, request.name, 0, request.pixels, request.kind, metrics);
}

FontResolver* createPathFontResolver(Composer& composer) {
    return composer.pool->make<PathFontResolverImpl>(composer);
}
