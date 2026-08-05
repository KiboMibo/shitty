/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "font_pack.h"
#include "grapheme.h"

#include "composer.h"
#include "font_face.h"
#include "font_resolver.h"
#include "unicode_map.h"
#include "utf8.h"

#include <std/lib/buffer.h>
#include <std/sym/i_map.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/sys/throw.h>

#include <errno.h>

using namespace stl;

namespace {
    static constexpr u16 unresolvedFace = 0;
    static constexpr u16 uncoveredFace = UINT16_MAX;

    struct FontpackImpl final: public Fontpack {
        FontpackImpl(Composer& composer, ObjPool& pool, const StringView* names, size_t nameCount, u16 size);

        u16 getPx() const override;
        u16 getPy() const override;
        bool hasBold() const override;
        bool hasItalic() const override;
        bool hasBoldItalic() const override;

        // A pictogram behind a width-one codepoint can ink far past its
        Font* createOptional(Composer& composer, ObjPool& pool, StringView name, u16 size, FontStyle style, FontKind kind, FontMetrics metrics);
        Font* select(FontStyle style) const noexcept;
        Font* faceAt(u16 index) const noexcept;
        bool coversAll(Font* font, const u32* codepoints, size_t count) const;
        Font* resolveFace(const u32* codepoints, size_t count) override;
        Font* styledFace(Font* face, FontStyle style) const override;

        Composer* composer_ = nullptr;
        ObjPool* pool_ = nullptr;
        u16 size_ = 0;
        FontMetrics metrics_;
        Font* regular_ = nullptr;
        Font* bold_ = nullptr;
        Font* italic_ = nullptr;
        Font* boldItalic_ = nullptr;
        Vector<Font*> fallbacks_;
        UnicodeMap<u16>* faceCache_ = nullptr;
    };

    // Joiners and variation selectors modify a cluster but are absent from
    // most cmaps; they do not participate in coverage matching.
    // The plane a cluster wants: an explicit variation selector rules,
    // a default-emoji base asks for color. Any means no preference.
    enum class PlaneWish {
        Any,
        Color,
        Mask
    };

    static PlaneWish clusterPlaneWish(const u32* codepoints, size_t count) {
        PlaneWish wish = PlaneWish::Any;
        for (size_t index = 0; index < count; ++index) {
            const u32 codepoint = codepoints[index];
            if (codepoint == 0xfe0f) {
                return PlaneWish::Color;
            }
            if (codepoint == 0xfe0e) {
                return PlaneWish::Mask;
            }
            if (emojiPresentation(codepoint)) {
                wish = PlaneWish::Color;
            }
        }
        return wish;
    }

    static bool significantCodepoint(u32 codepoint) {
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

FontpackImpl::FontpackImpl(Composer& composer, ObjPool& pool, const StringView* names, size_t nameCount, u16 size)
    : composer_(&composer)
    , pool_(&pool)
    , size_(size)
{
    const StringView primary = nameCount != 0 ? names[0] : StringView();
    regular_ = composer.loadFont(pool, {primary, size, FontStyle::Regular, FontKind::Primary}, metrics_);
    if (regular_ == nullptr) {
        Errno(EINVAL).raise(StringBuilder() << StringView(u8"no suitable font found for ") << primary);
    }

    bold_ = createOptional(composer, pool, primary, size, FontStyle::Bold, FontKind::Overlay, metrics_);
    italic_ = createOptional(composer, pool, primary, size, FontStyle::Italic, FontKind::Overlay, metrics_);
    boldItalic_ = createOptional(composer, pool, primary, size, FontStyle::BoldItalic, FontKind::Overlay, metrics_);
    if (bold_ == nullptr) {
        bold_ = regular_->synthesize(pool, FontStyle::Bold);
    }
    if (italic_ == nullptr) {
        italic_ = regular_->synthesize(pool, FontStyle::Italic);
    }
    if (boldItalic_ == nullptr) {
        boldItalic_ = regular_->synthesize(pool, FontStyle::BoldItalic);
    }

    for (size_t index = 1; index < nameCount; ++index) {
        Font* const fallback = createOptional(composer, pool, names[index], size, FontStyle::Regular, FontKind::Fallback, metrics_);
        if (fallback != nullptr) {
            fallbacks_.pushBack(fallback);
        }
    }

    // Every resolver may contribute implicit coverage fallbacks (the
    // embedded fonts arrive this way); they follow the configured list in
    // chain order.
    for (IntrusiveNode* node = composer.fontResolvers.mutFront(); node != composer.fontResolvers.mutEnd(); node = node->next) {
        FontResolver* const resolver = static_cast<FontResolver*>(node);
        for (size_t index = 0;; ++index) {
            FontFace* const face = resolver->fallback(index);
            if (face == nullptr) {
                break;
            }
            FontMetrics metrics = metrics_;
            Font* const font = composer.renderFace(pool, face, size, FontKind::Fallback, metrics);
            if (font != nullptr) {
                fallbacks_.pushBack(font);
            }
        }
    }

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

Font* FontpackImpl::styledFace(Font* face, FontStyle style) const {
    return face == regular_ ? select(style) : face;
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

    // An emoji-presentation cluster looks for a color face across the
    // whole chain first, so a monochrome font early in the system
    // fallback order cannot shadow a color emoji face behind it; an
    // explicit VS15 asks for the opposite.
    const PlaneWish wish = clusterPlaneWish(codepoints, count);
    if (wish != PlaneWish::Any) {
        const bool wantColor = wish == PlaneWish::Color;
        if (regular_->colored() == wantColor && coversAll(regular_, codepoints, count)) {
            if (cached != nullptr) {
                *cached = 1;
            }
            return regular_;
        }
        for (size_t index = 0; index < fallbacks_.length(); ++index) {
            Font* const fallback = fallbacks_[index];
            if (fallback->colored() == wantColor && coversAll(fallback, codepoints, count)) {
                if (cached != nullptr) {
                    *cached = (u16)(index + 2u);
                }
                return fallback;
            }
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

Fontpack* Fontpack::create(Composer& composer, ObjPool& pool, const StringView* names, size_t nameCount, u16 size) {
    return pool.make<FontpackImpl>(composer, pool, names, nameCount, size);
}
