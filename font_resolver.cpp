/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "font_resolver.h"

#include <std/lib/buffer.h>
#include <std/mem/obj_pool.h>

#include <fontconfig/fontconfig.h>

namespace stl {}

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

    StringView fontconfigFile(ObjPool* pool, StringView family, int weight, int slant) {
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

        FcChar8* file = nullptr;
        StringView path;
        if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch) {
            path = pool->intern(StringView((const char*)(file)));
        }
        FcPatternDestroy(match);
        return path;
    }
}

FontVariants resolveFontconfig(ObjPool* pool, StringView family) {
    FontVariants variants;
    if (family.memChr('/')) {
        variants.regular = pool->intern(family);
        return variants;
    }
    variants.regular = fontconfigFile(pool, family, FC_WEIGHT_REGULAR, FC_SLANT_ROMAN);
    if (variants.regular.empty()) {
        return variants;
    }

    StringView path = fontconfigFile(pool, family, FC_WEIGHT_BOLD, FC_SLANT_ROMAN);
    if (!path.empty() && path != variants.regular) {
        variants.bold = path;
    }
    path = fontconfigFile(pool, family, FC_WEIGHT_REGULAR, FC_SLANT_ITALIC);
    if (!path.empty() && path != variants.regular) {
        variants.italic = path;
    }
    path = fontconfigFile(pool, family, FC_WEIGHT_BOLD, FC_SLANT_ITALIC);
    if (!path.empty() && path != variants.regular && path != variants.bold && path != variants.italic) {
        variants.boldItalic = path;
    }
    return variants;
}

void finalizeFontconfig() noexcept {
    if (fontconfigInitialized) {
        FcFini();
        fontconfigInitialized = false;
    }
}
