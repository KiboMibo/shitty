/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "font_pack.h"

#include "composer.h"
#include "font_resolver.h"
#include "utf8.h"

#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/sys/throw.h>

#include <errno.h>

namespace stl {}

using namespace stl;

namespace {
    struct FontpackImpl final: public Fontpack {
        FontpackImpl(Composer& composer, StringView fontname, StringView dwfontname);

        u16 getPx() const override;
        u16 getPy() const override;
        bool hasBold() const override;
        bool hasItalic() const override;
        bool hasBoldItalic() const override;
        bool hasDoubleWidth() const override;
        FontGlyph glyph(u32 id, FontStyle style, bool doubleWidth) override;

        Font* createOptional(Composer& composer, StringView filename, FontKind kind, FontMetrics metrics);
        Font* select(FontStyle style) const noexcept;
        FontGlyph fallback(Font* font, Font* base, u32 id);

        FontMetrics metrics_;
        Font* regular_ = nullptr;
        Font* bold_ = nullptr;
        Font* italic_ = nullptr;
        Font* boldItalic_ = nullptr;
        Font* doubleWidth_ = nullptr;
    };
}

FontpackImpl::FontpackImpl(Composer& composer, StringView fontname, StringView dwfontname) {
    const FontVariants variants = resolveFontconfig(composer.pool, fontname);
    if (variants.regular.empty()) {
        Errno(EINVAL).raise(StringBuilder() << StringView(u8"no suitable font found for ") << fontname);
    }

    regular_ = Font::create(composer, variants.regular, FontKind::Primary, metrics_);
    bold_ = createOptional(composer, variants.bold, FontKind::Overlay, metrics_);
    italic_ = createOptional(composer, variants.italic, FontKind::Overlay, metrics_);
    boldItalic_ = createOptional(composer, variants.boldItalic, FontKind::Overlay, metrics_);

    if (!dwfontname.empty()) {
        const FontVariants wideVariants = resolveFontconfig(composer.pool, dwfontname);
        FontMetrics wideMetrics{
            .width = (u16)(2 * metrics_.width),
            .height = metrics_.height,
            .baseline = metrics_.baseline,
        };
        doubleWidth_ = createOptional(composer, wideVariants.regular, FontKind::DoubleWidth, wideMetrics);
    }
}

Font* FontpackImpl::createOptional(Composer& composer, StringView filename, FontKind kind, FontMetrics metrics) {
    if (filename.empty()) {
        return nullptr;
    }
    try {
        return Font::create(composer, filename, kind, metrics);
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

FontGlyph FontpackImpl::fallback(Font* font, Font* base, u32 id) {
    FontGlyph result = font->glyph(id);
    if (result.len == 0 && font != base) {
        result = base->glyph(id);
        font = base;
    }
    if (result.len == 0 && id != Unicode_Replacement_Character) {
        result = font->glyph(Unicode_Replacement_Character);
    }
    if (result.len == 0 && id != Missing_Glyph_Marker) {
        result = font->glyph(Missing_Glyph_Marker);
    }
    return result;
}

FontGlyph FontpackImpl::glyph(u32 id, FontStyle style, bool doubleWidth) {
    if (doubleWidth) {
        return doubleWidth_ != nullptr ? fallback(doubleWidth_, doubleWidth_, id) : FontGlyph{};
    }
    return fallback(select(style), regular_, id);
}

Fontpack* Fontpack::create(Composer& composer, StringView fontname, StringView dwfontname) {
    return composer.pool->make<FontpackImpl>(composer, fontname, dwfontname);
}
