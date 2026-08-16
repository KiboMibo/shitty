/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "font_fontconfig.h"

#include "composer.h"
#include "font_face.h"
#include "font_resolver.h"

#include <std/lib/buffer.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/str/view.h>
#include <std/sys/throw.h>

#if defined(HAVE_FONTCONFIG)
    #include <fontconfig/fontconfig.h>
#endif

using namespace stl;

#if defined(HAVE_FONTCONFIG)
namespace {
    struct FontSource {
        Buffer filename;
        i32 index = 0;
        bool valid = false;
    };

    struct FontconfigResolverImpl final: public FontResolver {
        explicit FontconfigResolverImpl(Composer& composer);
        ~FontconfigResolverImpl() noexcept;

        FontFace* resolve(const FontRequest& request) override;
        FontFace* resolveCluster(const u32* codepoints, size_t count, FontPlane plane) override;

        bool initialize();
        void resolveSource(StringView family, FontStyle style, FontSource& source);
        bool matchedFamily(FcPattern* match, StringView family);

        Composer& composer_;
        FcConfig* config_ = nullptr;
        Buffer query_;
    };

    static bool genericFamily(StringView family) {
        return family == StringView(u8"monospace") || family == StringView(u8"sans-serif") || family == StringView(u8"serif") || family == StringView(u8"cursive") || family == StringView(u8"fantasy");
    }

    static bool sameSource(const FontSource& left, const FontSource& right) {
        return left.valid && right.valid && left.index == right.index && StringView(left.filename) == StringView(right.filename);
    }

    static void fontconfigStyle(FontStyle style, int& weight, int& slant) {
        weight = style == FontStyle::Bold || style == FontStyle::BoldItalic ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR;
        slant = style == FontStyle::Italic || style == FontStyle::BoldItalic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN;
    }
}

FontconfigResolverImpl::FontconfigResolverImpl(Composer& composer)
    : composer_(composer)
{
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

void FontconfigResolverImpl::resolveSource(StringView family, FontStyle style, FontSource& source) {
    if (!initialize()) {
        return;
    }

    FcPattern* pattern = FcPatternCreate();
    if (pattern == nullptr) {
        return;
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
        return;
    }
    if (!matchedFamily(match, family)) {
        FcPatternDestroy(match);
        return;
    }

    FcChar8* file = nullptr;
    if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch) {
        source.filename = Buffer(StringView((const char*)(file)));
        source.valid = true;
        int index = 0;
        if (FcPatternGetInteger(match, FC_INDEX, 0, &index) == FcResultMatch) {
            source.index = index;
        }
    }
    FcPatternDestroy(match);
}

// The system's answer for one uncovered cluster: match by charset so
// fontconfig picks whatever the host would use for these codepoints -
// CJK, emoji, any script the configured families miss - ahead of the
// embedded last resort in the resolver chain.
FontFace* FontconfigResolverImpl::resolveCluster(const u32* codepoints, size_t count, FontPlane plane) {
    if (count == 0 || !initialize()) {
        return nullptr;
    }
    FcPattern* pattern = FcPatternCreate();
    if (pattern == nullptr) {
        return nullptr;
    }
    FcCharSet* charset = FcCharSetCreate();
    if (charset == nullptr) {
        FcPatternDestroy(pattern);
        return nullptr;
    }
    for (size_t index = 0; index < count; ++index) {
        FcCharSetAddChar(charset, codepoints[index]);
    }
    FcPatternAddCharSet(pattern, FC_CHARSET, charset);
    FcPatternAddBool(pattern, FC_SCALABLE, FcTrue);
    #if defined(FC_COLOR)
    if (plane == FontPlane::Color) {
        FcPatternAddBool(pattern, FC_COLOR, FcTrue);
    } else if (plane == FontPlane::Mask) {
        FcPatternAddBool(pattern, FC_COLOR, FcFalse);
    }
    #else
    (void)(plane);
    #endif
    FcConfigSubstitute(config_, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    FcResult result;
    FcPattern* const match = FcFontMatch(config_, pattern, &result);
    FcPatternDestroy(pattern);
    FcCharSetDestroy(charset);
    if (match == nullptr) {
        return nullptr;
    }
    // FcFontMatch always answers; only a match that actually covers the
    // cluster settles anything.
    FcCharSet* supported = nullptr;
    bool covers = FcPatternGetCharSet(match, FC_CHARSET, 0, &supported) == FcResultMatch;
    for (size_t index = 0; covers && index < count; ++index) {
        covers = FcCharSetHasChar(supported, codepoints[index]);
    }
    FcChar8* file = nullptr;
    int faceIndex = 0;
    if (covers && FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch) {
        FcPatternGetInteger(match, FC_INDEX, 0, &faceIndex);
        query_.reset();
        const StringView path((const char*)(file));
        query_.append(path.data(), path.length());
        FcPatternDestroy(match);
        try {
            return openFontFile(StringView(query_), faceIndex);
        } catch (Exception&) {
            return nullptr;
        }
    }
    FcPatternDestroy(match);
    return nullptr;
}

FontFace* FontconfigResolverImpl::resolve(const FontRequest& request) {
    if (request.name.memChr('/') || request.name.memChr('\\')) {
        return nullptr;
    }

    FontSource source;
    resolveSource(request.name, request.style, source);
    if (!source.valid) {
        return nullptr;
    }
    if (request.style != FontStyle::Regular) {
        FontSource regular;
        resolveSource(request.name, FontStyle::Regular, regular);
        if (sameSource(source, regular)) {
            return nullptr;
        }
    }
    if (request.style == FontStyle::BoldItalic) {
        FontSource bold;
        FontSource italic;
        resolveSource(request.name, FontStyle::Bold, bold);
        resolveSource(request.name, FontStyle::Italic, italic);
        if (sameSource(source, bold) || sameSource(source, italic)) {
            return nullptr;
        }
    }
    return openFontFile(StringView(source.filename), source.index);
}
#endif

FontResolver* createFontconfigResolver(Composer& composer) {
#if defined(HAVE_FONTCONFIG)
    return composer.pool->make<FontconfigResolverImpl>(composer);
#else
    (void)(composer);
    return nullptr;
#endif
}
