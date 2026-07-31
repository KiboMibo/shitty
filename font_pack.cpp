/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "font_pack.h"

#include "composer.h"
#include "font_embedded.h"
#include "font_freetype.h"
#include "font_resolver.h"
#include "unicode_map.h"
#include "utf8.h"

#include <std/lib/buffer.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/sys/throw.h>

#include <errno.h>

using namespace stl;

namespace {
    constexpr u16 unresolvedFace = 0;
    constexpr u16 uncoveredFace = UINT16_MAX;

    struct FontpackImpl final: public Fontpack {
        FontpackImpl(Composer& composer, ObjPool& pool, const StringView* names, size_t nameCount, u16 size);

        u16 getPx() const override;
        u16 getPy() const override;
        bool hasBold() const override;
        bool hasItalic() const override;
        bool hasBoldItalic() const override;
        FontGlyph glyph(const u32* codepoints, size_t count, FontStyle style, bool doubleWidth) override;

        Font* createOptional(Composer& composer, ObjPool& pool, StringView name, u16 size, FontStyle style, FontKind kind, FontMetrics metrics);
        Font* select(FontStyle style) const noexcept;
        Font* faceAt(u16 index) const noexcept;
        bool coversAll(Font* font, const u32* codepoints, size_t count) const;
        Font* resolveFace(const u32* codepoints, size_t count);
        FontGlyph render(Font* face, const u32* codepoints, size_t count, FontStyle style, u16 cells);
        FontGlyph missingBox(u16 cells);

        FontMetrics metrics_;
        Font* regular_ = nullptr;
        Font* bold_ = nullptr;
        Font* italic_ = nullptr;
        Font* boldItalic_ = nullptr;
        Vector<Font*> fallbacks_;
        UnicodeMap<u16>* faceCache_ = nullptr;
        Buffer boxBitmap_;
    };

    // Joiners and variation selectors modify a cluster but are absent from
    // most cmaps; they do not participate in coverage matching.
    bool significantCodepoint(u32 codepoint) {
        if (codepoint == 0x200d) {
            return false;
        }
        if (codepoint >= 0xfe00 && codepoint <= 0xfe0f) {
            return false;
        }
        if (codepoint >= 0xe0100 && codepoint <= 0xe01ef) {
            return false;
        }
        return true;
    }
}

FontpackImpl::FontpackImpl(Composer& composer, ObjPool& pool, const StringView* names, size_t nameCount, u16 size) {
    const StringView primary = nameCount != 0 ? names[0] : StringView();
    regular_ = composer.loadFont(pool, {primary, size, FontStyle::Regular, FontKind::Primary}, metrics_);
    if (regular_ == nullptr) {
        Errno(EINVAL).raise(StringBuilder() << StringView(u8"no suitable font found for ") << primary);
    }

    bold_ = createOptional(composer, pool, primary, size, FontStyle::Bold, FontKind::Overlay, metrics_);
    italic_ = createOptional(composer, pool, primary, size, FontStyle::Italic, FontKind::Overlay, metrics_);
    boldItalic_ = createOptional(composer, pool, primary, size, FontStyle::BoldItalic, FontKind::Overlay, metrics_);

    for (size_t index = 1; index < nameCount; ++index) {
        Font* const fallback = createOptional(composer, pool, names[index], size, FontStyle::Regular, FontKind::Fallback, metrics_);
        if (fallback != nullptr) {
            fallbacks_.pushBack(fallback);
        }
    }

#if defined(HAVE_FREETYPE) && defined(HAVE_HARFBUZZ)
    const EmbeddedFontBlob embedded[] = {
        embeddedEmojiFont(),
        embeddedMonoFont(),
        embeddedEmojiTextFont(),
    };
    for (const EmbeddedFontBlob& blob : embedded) {
        if (blob.data == nullptr) {
            continue;
        }
        try {
            FontMetrics metrics = metrics_;
            fallbacks_.pushBack(createFreeTypeMemoryFont(pool, blob.data, blob.size, 0, size, FontKind::Fallback, metrics));
        } catch (Exception&) {
        }
    }
#endif

    faceCache_ = UnicodeMap<u16>::create(pool);
}

Font* FontpackImpl::createOptional(Composer& composer, ObjPool& pool, StringView name, u16 size, FontStyle style, FontKind kind, FontMetrics metrics) {
    try {
        return composer.loadFont(pool, {name, size, style, kind}, metrics);
    } catch (Exception&) {
        return nullptr;
    }
}

u16 FontpackImpl::getPx() const {
    return metrics_.width;
}

u16 FontpackImpl::getPy() const {
    return metrics_.height;
}

bool FontpackImpl::hasBold() const {
    return bold_ != nullptr;
}

bool FontpackImpl::hasItalic() const {
    return italic_ != nullptr;
}

bool FontpackImpl::hasBoldItalic() const {
    return boldItalic_ != nullptr;
}

Font* FontpackImpl::select(FontStyle style) const noexcept {
    switch (style) {
        case FontStyle::Bold:
            return bold_ != nullptr ? bold_ : regular_;
        case FontStyle::Italic:
            return italic_ != nullptr ? italic_ : regular_;
        case FontStyle::BoldItalic:
            if (boldItalic_ != nullptr) {
                return boldItalic_;
            }
            if (italic_ != nullptr) {
                return italic_;
            }
            return bold_ != nullptr ? bold_ : regular_;
        case FontStyle::Regular:
            return regular_;
    }
    return regular_;
}

Font* FontpackImpl::faceAt(u16 index) const noexcept {
    return index == 0 ? regular_ : fallbacks_[index - 1u];
}

bool FontpackImpl::coversAll(Font* font, const u32* codepoints, size_t count) const {
    for (size_t index = 0; index < count; ++index) {
        if (significantCodepoint(codepoints[index]) && !font->covers(codepoints[index])) {
            return false;
        }
    }
    return true;
}

Font* FontpackImpl::resolveFace(const u32* codepoints, size_t count) {
    u16* cached = nullptr;
    if (count == 1) {
        cached = &(*faceCache_)[codepoints[0]];
        if (*cached == uncoveredFace) {
            return nullptr;
        }
        if (*cached != unresolvedFace) {
            return faceAt((u16)(*cached - 1u));
        }
    }

    if (coversAll(regular_, codepoints, count)) {
        if (cached != nullptr) {
            *cached = 1;
        }
        return regular_;
    }
    for (size_t index = 0; index < fallbacks_.length(); ++index) {
        Font* const fallback = fallbacks_[index];
        if (coversAll(fallback, codepoints, count)) {
            if (cached != nullptr) {
                *cached = (u16)(index + 2u);
            }
            return fallback;
        }
    }
    if (cached != nullptr) {
        *cached = uncoveredFace;
    }
    return nullptr;
}

FontGlyph FontpackImpl::render(Font* face, const u32* codepoints, size_t count, FontStyle style, u16 cells) {
    Font* const styled = face == regular_ ? select(style) : face;
    FontGlyph result = styled->glyph(codepoints, count, cells);
    if (result.len == 0 && styled != face) {
        result = face->glyph(codepoints, count, cells);
    }
    return result;
}

// The hollow frame every cluster no face covers renders as; matches the
// frame the reference renderer historically drew for lost glyphs.
FontGlyph FontpackImpl::missingBox(u16 cells) {
    const int width = cells * metrics_.width;
    const int height = metrics_.height;
    boxBitmap_.zero((size_t)(width)*height);
    auto* pixels = (u8*)(boxBitmap_.mutData());
    for (int y = 1; y + 1 < height; ++y) {
        for (int x = 1; x + 1 < width; ++x) {
            if (x == 1 || x + 2 == width || y == 1 || y + 2 == height) {
                pixels[(size_t)(y)*width + x] = 179;
            }
        }
    }
    return {
        .data = boxBitmap_.data(),
        .len = boxBitmap_.used(),
        .color = false,
    };
}

FontGlyph FontpackImpl::glyph(const u32* codepoints, size_t count, FontStyle style, bool doubleWidth) {
    const u16 cells = doubleWidth ? 2 : 1;
    if (count == 0 || (count == 1 && codepoints[0] == Missing_Glyph_Marker)) {
        return missingBox(cells);
    }

    FontGlyph result{};
    if (Font* const face = resolveFace(codepoints, count)) {
        result = render(face, codepoints, count, style, cells);
    }
    const u32 replacement = Unicode_Replacement_Character;
    if (result.len == 0 && (count != 1 || codepoints[0] != replacement)) {
        if (Font* const face = resolveFace(&replacement, 1)) {
            result = render(face, &replacement, 1, style, cells);
        }
    }
    if (result.len == 0) {
        result = missingBox(cells);
    }
    return result;
}

Fontpack* Fontpack::create(Composer& composer, ObjPool& pool, const StringView* names, size_t nameCount, u16 size) {
    return pool.make<FontpackImpl>(composer, pool, names, nameCount, size);
}
