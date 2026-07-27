/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "font_pack.h"

#include "utf8.h"
#include "composer.h"
#include "font_resolver.h"

#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/sys/throw.h>

#include <errno.h>

using namespace stl;

namespace {
    struct FontpackImpl final: public Fontpack {
        FontpackImpl(Composer& composer, ObjPool& pool, StringView fontname, StringView dwfontname, u16 size);

        u16 getPx() const override;
        u16 getPy() const override;
        bool hasBold() const override;
        bool hasItalic() const override;
        bool hasBoldItalic() const override;
        bool hasDoubleWidth() const override;
        FontGlyph glyph(const u32* codepoints, size_t count, FontStyle style, bool doubleWidth) override;

        Font* createOptional(Composer& composer, ObjPool& pool, StringView name, u16 size, FontStyle style, FontKind kind, FontMetrics metrics);
        Font* select(FontStyle style) const noexcept;
        FontGlyph fallback(Font* font, Font* base, const u32* codepoints, size_t count);

        FontMetrics metrics_;
        Font* regular_ = nullptr;
        Font* bold_ = nullptr;
        Font* italic_ = nullptr;
        Font* boldItalic_ = nullptr;
        Font* doubleWidth_ = nullptr;
    };
}

FontpackImpl::FontpackImpl(Composer& composer, ObjPool& pool, StringView fontname, StringView dwfontname, u16 size) {
    regular_ = composer.loadFont(pool, {fontname, size, FontStyle::Regular, FontKind::Primary}, metrics_);
    if (regular_ == nullptr) {
        Errno(EINVAL).raise(StringBuilder() << StringView(u8"no suitable font found for ") << fontname);
    }

    bold_ = createOptional(composer, pool, fontname, size, FontStyle::Bold, FontKind::Overlay, metrics_);
    italic_ = createOptional(composer, pool, fontname, size, FontStyle::Italic, FontKind::Overlay, metrics_);
    boldItalic_ = createOptional(composer, pool, fontname, size, FontStyle::BoldItalic, FontKind::Overlay, metrics_);

    if (!dwfontname.empty()) {
        FontMetrics wideMetrics{
            .width = (u16)(2 * metrics_.width),
            .height = metrics_.height,
            .baseline = metrics_.baseline,
        };
        doubleWidth_ = createOptional(composer, pool, dwfontname, size, FontStyle::Regular, FontKind::DoubleWidth, wideMetrics);
    }
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

bool FontpackImpl::hasDoubleWidth() const {
    return doubleWidth_ != nullptr;
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

FontGlyph FontpackImpl::fallback(Font* font, Font* base, const u32* codepoints, size_t count) {
    FontGlyph result = font->glyph(codepoints, count);
    if (result.len == 0 && font != base) {
        result = base->glyph(codepoints, count);
        font = base;
    }
    const u32 replacement = Unicode_Replacement_Character;
    if (result.len == 0 && (count != 1 || codepoints[0] != replacement)) {
        result = font->glyph(&replacement, 1);
    }
    const u32 missing = Missing_Glyph_Marker;
    if (result.len == 0 && (count != 1 || codepoints[0] != missing)) {
        result = font->glyph(&missing, 1);
    }
    return result;
}

FontGlyph FontpackImpl::glyph(const u32* codepoints, size_t count, FontStyle style, bool doubleWidth) {
    if (doubleWidth) {
        return doubleWidth_ != nullptr ? fallback(doubleWidth_, doubleWidth_, codepoints, count) : FontGlyph{};
    }
    return fallback(select(style), regular_, codepoints, count);
}

Fontpack* Fontpack::create(Composer& composer, ObjPool& pool, StringView fontname, StringView dwfontname, u16 size) {
    return pool.make<FontpackImpl>(composer, pool, fontname, dwfontname, size);
}
