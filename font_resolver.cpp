/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "font_resolver.h"

#include <std/lib/buffer.h>
#include <std/mem/obj_pool.h>

#include <fontconfig/fontconfig.h>

using namespace stl;

namespace {
    bool fontconfigInitialized = false;

    bool initializeFontconfig() {
        if (!fontconfigInitialized) {
            fontconfigInitialized = FcInit();
        }
        return fontconfigInitialized;
    }

    bool genericFamily(StringView family) {
        return family == StringView(u8"monospace") || family == StringView(u8"sans-serif") || family == StringView(u8"serif") || family == StringView(u8"cursive") || family == StringView(u8"fantasy");
    }

    bool matchedFamily(FcPattern* match, StringView family) {
        if (genericFamily(family)) {
            return true;
        }
        Buffer requested(family);
        for (int index = 0;; ++index) {
            FcChar8* matched = nullptr;
            if (FcPatternGetString(match, FC_FAMILY, index, &matched) != FcResultMatch) {
                return false;
            }
            if (FcStrCmpIgnoreCase(matched, (const FcChar8*)(requested.cStr())) == 0) {
                return true;
            }
        }
    }

    FontSource fontconfigSource(ObjPool* pool, StringView family, int weight, int slant) {
        if (!initializeFontconfig()) {
            return {};
        }

        FcPattern* pattern = FcPatternCreate();
        if (pattern == nullptr) {
            return {};
        }
        Buffer familyBuffer(family);
        FcPatternAddString(pattern, FC_FAMILY, (const FcChar8*)(familyBuffer.cStr()));
        FcPatternAddInteger(pattern, FC_WEIGHT, weight);
        FcPatternAddInteger(pattern, FC_SLANT, slant);
        FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
        FcDefaultSubstitute(pattern);

        FcResult result;
        FcPattern* match = FcFontMatch(nullptr, pattern, &result);
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
            source.filename = pool->intern(StringView((const char*)(file)));
            int index = 0;
            if (FcPatternGetInteger(match, FC_INDEX, 0, &index) == FcResultMatch) {
                source.index = index;
            }
        }
        FcPatternDestroy(match);
        return source;
    }

    bool sameSource(FontSource left, FontSource right) {
        return left.index == right.index && left.filename == right.filename;
    }
}

FontVariants resolveFontconfig(ObjPool* pool, StringView family) {
    FontVariants variants;
    if (family.memChr('/')) {
        variants.regular.filename = pool->intern(family);
        return variants;
    }
    variants.regular = fontconfigSource(pool, family, FC_WEIGHT_REGULAR, FC_SLANT_ROMAN);
    if (variants.regular.filename.empty()) {
        return variants;
    }

    FontSource source = fontconfigSource(pool, family, FC_WEIGHT_BOLD, FC_SLANT_ROMAN);
    if (!source.filename.empty() && !sameSource(source, variants.regular)) {
        variants.bold = source;
    }
    source = fontconfigSource(pool, family, FC_WEIGHT_REGULAR, FC_SLANT_ITALIC);
    if (!source.filename.empty() && !sameSource(source, variants.regular)) {
        variants.italic = source;
    }
    source = fontconfigSource(pool, family, FC_WEIGHT_BOLD, FC_SLANT_ITALIC);
    if (!source.filename.empty() && !sameSource(source, variants.regular) && !sameSource(source, variants.bold) && !sameSource(source, variants.italic)) {
        variants.boldItalic = source;
    }
    return variants;
}

void finalizeFontconfig() noexcept {
    if (fontconfigInitialized) {
        FcFini();
        fontconfigInitialized = false;
    }
}
