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
        FontFace* fallback(size_t index) override;

        bool initialize();
        void resolveSource(StringView family, FontStyle style, FontSource& source);
        bool matchedFamily(FcPattern* match, StringView family);
        void buildFallbackList();

        Composer& composer_;
        FcConfig* config_ = nullptr;
        Buffer query_;
        // The system's coverage-trimmed preference order, resolved once:
        // pool-interned NUL-terminated paths plus collection indices.
        bool fallbackListReady_ = false;
        Vector<StringView> fallbackFiles_;
        Vector<i32> fallbackIndices_;
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

// The implicit coverage chain: everything the system would fall back
// through, in fontconfig's own preference order, trimmed to fonts that
// add coverage. This is what serves CJK, emoji, and every other script
// the configured families miss - ahead of the embedded last resort.
void FontconfigResolverImpl::buildFallbackList() {
    fallbackListReady_ = true;
    if (!initialize()) {
        return;
    }
    FcPattern* pattern = FcPatternCreate();
    if (pattern == nullptr) {
        return;
    }
    FcPatternAddBool(pattern, FC_SCALABLE, FcTrue);
    FcConfigSubstitute(config_, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    FcResult result;
    FcFontSet* const sorted = FcFontSort(config_, pattern, FcTrue, nullptr, &result);
    FcPatternDestroy(pattern);
    if (sorted == nullptr) {
        return;
    }
    // The trim keeps only fonts that widen coverage; the cap bounds the
    // per-fontpack face load on hosts with sprawling font sets.
    constexpr size_t fallbackLimit = 64;
    for (int at = 0; at < sorted->nfont && fallbackFiles_.length() < fallbackLimit; ++at) {
        FcChar8* file = nullptr;
        if (FcPatternGetString(sorted->fonts[at], FC_FILE, 0, &file) != FcResultMatch) {
            continue;
        }
        int faceIndex = 0;
        FcPatternGetInteger(sorted->fonts[at], FC_INDEX, 0, &faceIndex);
        fallbackFiles_.pushBack(composer_.pool->intern(StringView((const char*)(file))));
        fallbackIndices_.pushBack((i32)(faceIndex));
    }
    FcFontSetDestroy(sorted);
}

FontFace* FontconfigResolverImpl::fallback(size_t index) {
    if (!fallbackListReady_) {
        buildFallbackList();
    }
    if (index >= fallbackFiles_.length()) {
        return nullptr;
    }
    try {
        return openFontFile(fallbackFiles_[index], fallbackIndices_[index]);
    } catch (Exception&) {
        // A file that vanished since the scan; the chain ends here rather
        // than failing the whole fontpack build.
        return nullptr;
    }
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
