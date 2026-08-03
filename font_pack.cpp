/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "font_pack.h"

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

        // A pictogram behind a width-one codepoint can ink far past its
        // cell; the same face re-rendered at a smaller pixel size fits it.
        // Keyed by (face id << 16) | pixels; IntMap runs the key through
        // splitMix64. A null font caches the failure.
        struct FittedFont {
            Font* font;
            FontMetrics metrics;
        };

        Font* createOptional(Composer& composer, ObjPool& pool, StringView name, u16 size, FontStyle style, FontKind kind, FontMetrics metrics);
        Font* select(FontStyle style) const noexcept;
        Font* faceAt(u16 index) const noexcept;
        bool coversAll(Font* font, const u32* codepoints, size_t count) const;
        Font* resolveFace(const u32* codepoints, size_t count) override;
        FontGlyph render(Font* face, const u32* codepoints, size_t count, FontStyle style, u16 cells);
        FontGlyph fitOverflow(Font* font, const u32* codepoints, size_t count, u16 cells, FontGlyph result);
        const FittedFont* fittedFont(FontFace* face, u16 pixels);
        FontGlyph centered(const FontGlyph& glyph, u32 sourceCanvas, u32 sourceRows, u32 canvas, u32 rows, u32 left, u32 right);
        FontGlyph missingBox(u16 cells);

        Composer* composer_ = nullptr;
        ObjPool* pool_ = nullptr;
        u16 size_ = 0;
        IntMap<FittedFont> fitted_;
        Buffer fitBitmap_;
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

FontpackImpl::FontpackImpl(Composer& composer, ObjPool& pool, const StringView* names, size_t nameCount, u16 size)
    : composer_(&composer)
    , pool_(&pool)
    , size_(size)
    , fitted_(&pool)
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

namespace {
    // Ink extent over a mask canvas; false when blank. The threshold skips
    // antialiasing dust.
    bool maskInk(const FontGlyph& glyph, u32 canvas, u32 rows, u32& left, u32& right) {
        const u8* const data = (const u8*)(glyph.data);
        left = canvas;
        right = 0;
        for (u32 row = 0; row < rows; ++row) {
            const u8* const line = data + (size_t)(row)*canvas;
            for (u32 x = 0; x < canvas; ++x) {
                if (line[x] > 8) {
                    left = x < left ? x : left;
                    right = x > right ? x : right;
                }
            }
        }
        return left <= right;
    }

    bool touchesRightEdge(const FontGlyph& glyph, u32 canvas, u32 rows) {
        const u8* const data = (const u8*)(glyph.data);
        for (u32 row = 0; row < rows; ++row) {
            if (data[(size_t)(row)*canvas + canvas - 1] > 8) {
                return true;
            }
        }
        return false;
    }
}

const FontpackImpl::FittedFont* FontpackImpl::fittedFont(FontFace* face, u16 pixels) {
    const u64 key = ((u64)(face->id()) << 16) | pixels;
    if (const FittedFont* const cached = fitted_.find(key)) {
        return cached->font != nullptr ? cached : nullptr;
    }
    Font* font = nullptr;
    // Primary kind honours the size request verbatim and reports the
    // candidate's own metrics - its glyphs render on the candidate's
    // canvas, not the pack's.
    FontMetrics rendered = metrics_;
    try {
        font = composer_->renderFace(*pool_, face, pixels, FontKind::Primary, rendered);
    } catch (Exception&) {
    }
    const FittedFont* const entry = fitted_.insert(key, font, rendered);
    return font != nullptr ? entry : nullptr;
}

FontGlyph FontpackImpl::centered(const FontGlyph& glyph, u32 sourceCanvas, u32 sourceRows, u32 canvas, u32 rows, u32 left, u32 right) {
    // The candidate rendered on its own canvas; blit its ink into the
    // pack's cell, centered on both axes.
    const u32 ink = right - left + 1;
    const i32 shiftX = (i32)((canvas - ink) / 2) - (i32)(left);
    const i32 shiftY = ((i32)(rows) - (i32)(sourceRows)) / 2;
    fitBitmap_.zero((size_t)(canvas)*rows);
    auto* const destination = (u8*)(fitBitmap_.mutData());
    const u8* const bytes = (const u8*)(glyph.data);
    for (u32 row = 0; row < sourceRows; ++row) {
        const i32 target = (i32)(row) + shiftY;
        if (target < 0 || target >= (i32)(rows)) {
            continue;
        }
        for (u32 x = left; x <= right; ++x) {
            destination[(size_t)(target)*canvas + (u32)((i32)(x) + shiftX)] = bytes[(size_t)(row)*sourceCanvas + x];
        }
    }
    return {
        .data = fitBitmap_.data(),
        .len = (size_t)(canvas)*rows,
        .color = false,
    };
}

namespace {
    // Nerd Fonts park their pictograms in the private-use planes; text,
    // box drawing and italic overhangs never fit-scale.
    bool privateUse(u32 codepoint) {
        return (codepoint >= 0xe000 && codepoint <= 0xf8ff) || (codepoint >= 0xf0000 && codepoint <= 0xffffd) || (codepoint >= 0x100000 && codepoint <= 0x10fffd);
    }
}

// A width-one private-use cluster whose ink is clipped at the cell edge -
// Nerd Font pictograms under eza --icons - is re-rendered through the
// same face at a smaller pixel size until its unclipped ink fits, then
// centered. Color glyphs scale in the rasterizer already.
FontGlyph FontpackImpl::fitOverflow(Font* font, const u32* codepoints, size_t count, u16 cells, FontGlyph result) {
    const u32 canvas = (u32)(cells)*metrics_.width;
    const u32 rows = metrics_.height;
    if (result.color || result.len == 0 || cells != 1 || !privateUse(codepoints[0]) || !touchesRightEdge(result, canvas, rows)) {
        return result;
    }
    FontFace* const face = font->face();
    if (face == nullptr) {
        return result;
    }
    // The true ink extent, probed on the widest canvas the contract has.
    // The probe reuses the font's bitmap, so the original render is redone
    // when the overshoot turns out to be antialiasing slop.
    const FontGlyph probe = font->glyph(codepoints, count, 2);
    u32 left = 0;
    u32 right = 0;
    if (probe.len == 0 || probe.color || !maskInk(probe, 2u * metrics_.width, rows, left, right)) {
        return font->glyph(codepoints, count, cells);
    }
    const u32 ink = right - left + 1;
    if (ink <= canvas + 2) {
        return font->glyph(codepoints, count, cells);
    }
    u16 pixels = (u16)((u64)(size_)*canvas / ink);
    for (; pixels >= 4; --pixels) {
        const FittedFont* const candidate = fittedFont(face, pixels);
        if (candidate == nullptr) {
            break;
        }
        // The candidate's own cell would clip the very overflow being
        // fixed; judge the attempt on its two-cell canvas, where the ink
        // is whole, and require clearance from that edge too.
        const u32 candidateCanvas = 2u * candidate->metrics.width;
        const FontGlyph attempt = candidate->font->glyph(codepoints, count, 2);
        if (attempt.len == 0 || attempt.color) {
            break;
        }
        u32 fittedLeft = 0;
        u32 fittedRight = 0;
        if (!maskInk(attempt, candidateCanvas, candidate->metrics.height, fittedLeft, fittedRight)) {
            break;
        }
        if (fittedRight + 1 < candidateCanvas && fittedRight - fittedLeft + 1 <= canvas - 2 && candidate->metrics.height <= rows) {
            return centered(attempt, candidateCanvas, candidate->metrics.height, canvas, rows, fittedLeft, fittedRight);
        }
    }
    return font->glyph(codepoints, count, cells);
}

FontGlyph FontpackImpl::render(Font* face, const u32* codepoints, size_t count, FontStyle style, u16 cells) {
    Font* const styled = face == regular_ ? select(style) : face;
    FontGlyph result = styled->glyph(codepoints, count, cells);
    Font* rendered = styled;
    if (result.len == 0 && styled != face) {
        result = face->glyph(codepoints, count, cells);
        rendered = face;
    }
    return fitOverflow(rendered, codepoints, count, cells, result);
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
