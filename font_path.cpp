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

#if defined(HAVE_FREETYPE) && defined(HAVE_HARFBUZZ)
namespace {
    struct PathFontResolverImpl final: public FontResolver {
        PathFontResolverImpl();

        Font* load(ObjPool& owner, const FontRequest& request, FontMetrics& metrics) override;
    };
}

PathFontResolverImpl::PathFontResolverImpl() {
}

Font* PathFontResolverImpl::load(ObjPool& owner, const FontRequest& request, FontMetrics& metrics) {
    if (request.style != FontStyle::Regular || (!request.name.memChr('/') && !request.name.memChr('\\'))) {
        return nullptr;
    }
    return createFreeTypeFont(owner, request.name, 0, request.pixels, request.kind, metrics);
}
#endif

FontResolver* createPathFontResolver(Composer& composer) {
#if defined(HAVE_FREETYPE) && defined(HAVE_HARFBUZZ)
    return composer.pool->make<PathFontResolverImpl>();
#else
    (void)(composer);
    return nullptr;
#endif
}
