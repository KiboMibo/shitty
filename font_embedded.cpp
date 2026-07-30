/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "font_embedded.h"

#include "composer.h"
#include "font_freetype.h"
#include "font_resolver.h"

#include <std/mem/obj_pool.h>

#if defined(HAVE_FREETYPE) && defined(HAVE_HARFBUZZ)
    #include "font_data.h"
#endif

using namespace stl;

#if defined(HAVE_FREETYPE) && defined(HAVE_HARFBUZZ)
namespace {
    struct EmbeddedFontResolverImpl final: public FontResolver {
        Font* load(ObjPool& owner, const FontRequest& request, FontMetrics& metrics) override;
    };
}

Font* EmbeddedFontResolverImpl::load(ObjPool& owner, const FontRequest& request, FontMetrics& metrics) {
    if (request.style != FontStyle::Regular) {
        return nullptr;
    }
    return createFreeTypeMemoryFont(owner, embeddedFontMono.data, embeddedFontMono.size, 0, request.pixels, request.kind, metrics);
}

EmbeddedFontBlob embeddedMonoFont() {
    return {embeddedFontMono.data, embeddedFontMono.size};
}

EmbeddedFontBlob embeddedEmojiFont() {
    return {embeddedFontEmoji.data, embeddedFontEmoji.size};
}

EmbeddedFontBlob embeddedEmojiTextFont() {
    return {embeddedFontEmojiText.data, embeddedFontEmojiText.size};
}
#else
EmbeddedFontBlob embeddedMonoFont() {
    return {};
}

EmbeddedFontBlob embeddedEmojiFont() {
    return {};
}

EmbeddedFontBlob embeddedEmojiTextFont() {
    return {};
}
#endif

FontResolver* createEmbeddedFontResolver(Composer& composer) {
#if defined(HAVE_FREETYPE) && defined(HAVE_HARFBUZZ)
    return composer.pool->make<EmbeddedFontResolverImpl>();
#else
    (void)(composer);
    return nullptr;
#endif
}
