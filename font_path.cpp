/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "font_path.h"

#include "composer.h"
#include "font_face.h"
#include "font_resolver.h"

#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    struct PathFontResolverImpl final: public FontResolver {
        PathFontResolverImpl();

        FontFace* resolve(const FontRequest& request) override;
    };
}

PathFontResolverImpl::PathFontResolverImpl() {
}

FontFace* PathFontResolverImpl::resolve(const FontRequest& request) {
    if (request.style != FontStyle::Regular || (!request.name.memChr('/') && !request.name.memChr('\\'))) {
        return nullptr;
    }
    return openFontFile(request.name, 0);
}

FontResolver* createPathFontResolver(Composer& composer) {
    return composer.pool->make<PathFontResolverImpl>();
}
