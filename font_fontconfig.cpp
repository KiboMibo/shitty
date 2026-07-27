/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "font_fontconfig.h"

#include "composer.h"
#include "font_freetype.h"
#include "font_resolver.h"

#include <std/lib/buffer.h>
#include <std/mem/obj_pool.h>

#if defined(SHITTY_HAS_FONTCONFIG)
    #include <fontconfig/fontconfig.h>
#endif

using namespace stl;

#if defined(SHITTY_HAS_FONTCONFIG)
namespace {
    struct FontSource {
        StringView filename;
        i32 index = 0;
    };

    struct FontconfigResolverImpl final: public FontResolver {
        explicit FontconfigResolverImpl(Composer& composer);
        ~FontconfigResolverImpl() noexcept;

        Font* load(ObjPool& owner, const FontRequest& request, FontMetrics& metrics) override;

        bool initialize();
        FontSource resolve(ObjPool& owner, StringView family, FontStyle style);
        bool matchedFamily(FcPattern* match, StringView family);

        FcConfig* config_ = nullptr;
        Buffer query_;
    };

    bool genericFamily(StringView family) {
        return family == StringView(u8"monospace") || family == StringView(u8"sans-serif") || family == StringView(u8"serif") || family == StringView(u8"cursive") || family == StringView(u8"fantasy");
    }

    bool sameSource(FontSource left, FontSource right) {
        return left.index == right.index && left.filename == right.filename;
    }

    void fontconfigStyle(FontStyle style, int& weight, int& slant) {
        weight = style == FontStyle::Bold || style == FontStyle::BoldItalic ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR;
        slant = style == FontStyle::Italic || style == FontStyle::BoldItalic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN;
    }
}

FontconfigResolverImpl::FontconfigResolverImpl(Composer& composer) {
    composer.fontResolvers.pushBack(this);
}

FontconfigResolverImpl::~FontconfigResolverImpl() noexcept {
    if (config_ != nullptr) {
        FcConfigDestroy(config_);
    }
}

bool FontconfigResolverImpl::initialize() {
    if (config_ == nullptr) {
        config_ = FcInitLoadConfigAndFonts();
    }
    return config_ != nullptr;
}

bool FontconfigResolverImpl::matchedFamily(FcPattern* match, StringView family) {
    if (genericFamily(family)) {
        return true;
    }
    for (int index = 0;; ++index) {
        FcChar8* matched = nullptr;
        if (FcPatternGetString(match, FC_FAMILY, index, &matched) != FcResultMatch) {
            return false;
        }
        if (FcStrCmpIgnoreCase(matched, (const FcChar8*)(query_.cStr())) == 0) {
            return true;
        }
    }
}

FontSource FontconfigResolverImpl::resolve(ObjPool& owner, StringView family, FontStyle style) {
    if (!initialize()) {
        return {};
    }

    FcPattern* pattern = FcPatternCreate();
    if (pattern == nullptr) {
        return {};
    }
    query_.reset();
    query_.append(family.data(), family.length());
    int weight = 0;
    int slant = 0;
    fontconfigStyle(style, weight, slant);
    FcPatternAddString(pattern, FC_FAMILY, (const FcChar8*)(query_.cStr()));
    FcPatternAddInteger(pattern, FC_WEIGHT, weight);
    FcPatternAddInteger(pattern, FC_SLANT, slant);
    FcConfigSubstitute(config_, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    FcResult result;
    FcPattern* match = FcFontMatch(config_, pattern, &result);
    FcPatternDestroy(pattern);
    if (match == nullptr) {
        return {};
    }
    if (!matchedFamily(match, family)) {
        FcPatternDestroy(match);
        return {};
    }

    FontSource source;
    FcChar8* file = nullptr;
    if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch) {
        source.filename = owner.intern(StringView((const char*)(file)));
        int index = 0;
        if (FcPatternGetInteger(match, FC_INDEX, 0, &index) == FcResultMatch) {
            source.index = index;
        }
    }
    FcPatternDestroy(match);
    return source;
}

Font* FontconfigResolverImpl::load(ObjPool& owner, const FontRequest& request, FontMetrics& metrics) {
    if (request.name.memChr('/') || request.name.memChr('\\')) {
        return nullptr;
    }

    const FontSource source = resolve(owner, request.name, request.style);
    if (source.filename.empty()) {
        return nullptr;
    }
    if (request.style != FontStyle::Regular) {
        const FontSource regular = resolve(owner, request.name, FontStyle::Regular);
        if (sameSource(source, regular)) {
            return nullptr;
        }
    }
    if (request.style == FontStyle::BoldItalic) {
        const FontSource bold = resolve(owner, request.name, FontStyle::Bold);
        const FontSource italic = resolve(owner, request.name, FontStyle::Italic);
        if (sameSource(source, bold) || sameSource(source, italic)) {
            return nullptr;
        }
    }
    return createFreeTypeFont(owner, source.filename, source.index, request.pixels, request.kind, metrics);
}
#endif

FontResolver* createFontconfigResolver(Composer& composer) {
#if defined(SHITTY_HAS_FONTCONFIG)
    return composer.pool->make<FontconfigResolverImpl>(composer);
#else
    (void)(composer);
    return nullptr;
#endif
}
