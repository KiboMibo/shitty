#include "font_resolver.h"

#include <fontconfig/fontconfig.h>

namespace {
    std::string fontconfigFile(const std::string& family, int weight, int slant) {
        FcPattern* pattern = FcPatternCreate();
        if (!pattern) {
            return {};
        }
        FcPatternAddString(pattern, FC_FAMILY, (const FcChar8*)(family.c_str()));
        FcPatternAddInteger(pattern, FC_WEIGHT, weight);
        FcPatternAddInteger(pattern, FC_SLANT, slant);
        FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
        FcDefaultSubstitute(pattern);
        FcResult result;
        FcPattern* match = FcFontMatch(nullptr, pattern, &result);
        FcPatternDestroy(pattern);
        if (!match) {
            return {};
        }
        FcChar8* file = nullptr;
        std::string path;
        if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch) {
            path = (const char*)(file);
        }
        FcPatternDestroy(match);
        return path;
    }
}

FontVariants resolveFontconfig(const std::string& family) {
    FontVariants variants;
    variants.regular = fontconfigFile(family, FC_WEIGHT_REGULAR, FC_SLANT_ROMAN);
    if (variants.regular.empty()) {
        return variants;
    }
    std::string path = fontconfigFile(family, FC_WEIGHT_BOLD, FC_SLANT_ROMAN);
    if (!path.empty() && path != variants.regular) {
        variants.bold = path;
    }
    path = fontconfigFile(family, FC_WEIGHT_REGULAR, FC_SLANT_ITALIC);
    if (!path.empty() && path != variants.regular) {
        variants.italic = path;
    }
    path = fontconfigFile(family, FC_WEIGHT_BOLD, FC_SLANT_ITALIC);
    if (!path.empty() && path != variants.regular && path != variants.bold && path != variants.italic) {
        variants.boldItalic = path;
    }
    return variants;
}
