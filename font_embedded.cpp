/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "font_embedded.h"

#include "composer.h"
#include "font_data.h"
#include "font_face.h"
#include "font_resolver.h"

#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    // The bytes live in the binary, so a face outlives every consumer and
    // the count is a no-op.
    struct StaticFontFace final: public FontFace {
        StaticFontFace(const void* data, size_t size);

        u32 id() const noexcept override;
        const void* data() const override;
        size_t size() const override;
        i32 faceIndex() const override;

        void ref() noexcept override;
        i32 unref() noexcept override;
        i32 refCount() const noexcept override;

        const u32 id_ = nextFontFaceId();
        const void* data_;
        size_t size_;
    };

    struct EmbeddedFontResolverImpl final: public FontResolver {
        FontFace* resolve(const FontRequest& request) override;
        FontFace* fallback(size_t index) override;
    };
}

StaticFontFace::StaticFontFace(const void* data, size_t size)
    : data_(data)
    , size_(size)
{
}

u32 StaticFontFace::id() const noexcept {
    return id_;
}

const void* StaticFontFace::data() const {
    return data_;
}

size_t StaticFontFace::size() const {
    return size_;
}

i32 StaticFontFace::faceIndex() const {
    return 0;
}

void StaticFontFace::ref() noexcept {
}

i32 StaticFontFace::unref() noexcept {
    return 1;
}

i32 StaticFontFace::refCount() const noexcept {
    return 2;
}

namespace {
    StaticFontFace embeddedMono(embeddedFontMono.data, embeddedFontMono.size);
    StaticFontFace embeddedEmoji(embeddedFontEmoji.data, embeddedFontEmoji.size);
    StaticFontFace embeddedEmojiText(embeddedFontEmojiText.data, embeddedFontEmojiText.size);
}

FontFace* EmbeddedFontResolverImpl::resolve(const FontRequest& request) {
    if (request.style != FontStyle::Regular) {
        return nullptr;
    }
    return &embeddedMono;
}

FontFace* EmbeddedFontResolverImpl::fallback(size_t index) {
    switch (index) {
        case 0:
            return &embeddedEmoji;
        case 1:
            return &embeddedMono;
        case 2:
            return &embeddedEmojiText;
        default:
            return nullptr;
    }
}

FontResolver* createEmbeddedFontResolver(Composer& composer) {
    return composer.pool->make<EmbeddedFontResolverImpl>();
}
