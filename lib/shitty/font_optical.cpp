/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "font_optical.h"

#include "composer.h"
#include "font.h"
#include "font_face.h"
#include "font_renderer.h"

#include <std/lib/buffer.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>

#include <math.h>

using namespace stl;

namespace {
    struct RowProfile {
        double left = 0.0;
        double right = 0.0;
        double mass = 0.0;
    };

    struct OpticalGlyph {
        size_t mask = 0;
        int left = 0;
        int right = 0;
    };

    struct OpticalGap {
        double bearing = 0.0;
        double coefficient = 0.0;
        double distance = 0.0;
        int minimum = 0;
        int pixels = 0;
        bool fixed = false;
    };

    struct OpticalFont final: public Font {
        OpticalFont(Composer& composer, Font* base, FontMetrics metrics);

        void render(const u32* codepoints, size_t count, u16 cells, void* buf) override;
        bool covers(u32 codepoint) override;
        bool colored() const override;
        Font* synthesize(ObjPool& owner, FontStyle style) override;
        FontFace* face() override;

        bool simpleRun(const u32* codepoints, size_t count, u16 cells, size_t& glyphCount) const;
        bool materialize(const u32* codepoints, size_t count);
        bool profile(size_t glyph);
        double pairBearing(size_t left, size_t right) const;
        double edgeBearing(size_t glyph, bool left) const;
        bool collides(size_t left, size_t right, int distance) const;
        int minimumDistance(size_t left, size_t right) const;
        bool place(size_t count, int width);
        void draw(size_t count, u16 cells, u8* out) const;

        Composer& composer_;
        Font* base_;
        FontMetrics metrics_;
        Buffer masks_;
        Buffer scratch_;
        Vector<OpticalGlyph> glyphs_;
        Vector<RowProfile> profiles_;
        Vector<OpticalGap> gaps_;
        Vector<int> origins_;
    };

    struct OpticalFontRenderer final: public FontRenderer {
        OpticalFontRenderer(Composer& composer, FontRenderer* base);

        Font* render(ObjPool& owner, IntrusivePtr<FontFace> face, u16 pixels, FontKind kind, FontMetrics& metrics) override;

        Composer& composer_;
        FontRenderer* base_;
    };

    static bool simpleCodepoint(u32 codepoint) {
        if ((codepoint >= '0' && codepoint <= '9') || (codepoint >= 'A' && codepoint <= 'Z') || (codepoint >= 'a' && codepoint <= 'z')) {
            return true;
        }
        if ((codepoint >= 0x00c0 && codepoint <= 0x00d6) || (codepoint >= 0x00d8 && codepoint <= 0x00f6) || (codepoint >= 0x00f8 && codepoint <= 0x02af) || (codepoint >= 0x1e00 && codepoint <= 0x1eff)) {
            return true;
        }
        return (codepoint >= 0x0400 && codepoint <= 0x0481) || (codepoint >= 0x048a && codepoint <= 0x052f);
    }

    static double quantile(const u8* row, size_t width, double mass, double part) {
        const double target = mass * part;
        double accumulated = 0.0;
        for (size_t x = 0; x < width; ++x) {
            const double coverage = row[x];
            if (coverage != 0.0 && accumulated + coverage >= target) {
                return x + (target - accumulated) / coverage;
            }
            accumulated += coverage;
        }
        return width;
    }
}

OpticalFont::OpticalFont(Composer& composer, Font* base, FontMetrics metrics)
    : composer_(composer)
    , base_(base)
    , metrics_(metrics)
{
}

void OpticalFont::render(const u32* codepoints, size_t count, u16 cells, void* buf) {
    size_t glyphCount = 0;
    if (metrics_.width == 0 || metrics_.height == 0 || !simpleRun(codepoints, count, cells, glyphCount) || !materialize(codepoints, glyphCount) || !place(glyphCount, (int)(glyphCount * metrics_.width))) {
        base_->render(codepoints, count, cells, buf);
        return;
    }
    draw(glyphCount, cells, (u8*)(buf));
}

bool OpticalFont::covers(u32 codepoint) {
    return base_->covers(codepoint);
}

bool OpticalFont::colored() const {
    return base_->colored();
}

Font* OpticalFont::synthesize(ObjPool& owner, FontStyle style) {
    Font* const font = base_->synthesize(owner, style);
    return font != nullptr ? owner.make<OpticalFont>(composer_, font, metrics_) : nullptr;
}

FontFace* OpticalFont::face() {
    return base_->face();
}

bool OpticalFont::simpleRun(const u32* codepoints, size_t count, u16 cells, size_t& glyphCount) const {
    if (base_->colored() || count == 0 || count != cells) {
        return false;
    }
    glyphCount = count;
    while (glyphCount != 0 && codepoints[glyphCount - 1] == ' ') {
        --glyphCount;
    }
    if (glyphCount == 0) {
        return false;
    }
    for (size_t index = 0; index < glyphCount; ++index) {
        if (!simpleCodepoint(codepoints[index])) {
            return false;
        }
    }
    return true;
}

bool OpticalFont::materialize(const u32* codepoints, size_t count) {
    const size_t canvas = 3u * metrics_.width;
    const size_t glyphBytes = canvas * metrics_.height;
    masks_.zero(count * glyphBytes);
    glyphs_.zero(count);
    profiles_.zero(count * metrics_.height);
    const u32 padded[] = {' ', 0, ' '};
    for (size_t index = 0; index < count; ++index) {
        scratch_.zero(glyphBytes);
        u32 glyphText[3] = {padded[0], codepoints[index], padded[2]};
        base_->render(glyphText, 3, 3, scratch_.mutData());
        const size_t offset = index * glyphBytes;
        __builtin_memcpy((u8*)(masks_.mutData()) + offset, scratch_.data(), glyphBytes);
        glyphs_.mut(index).mask = offset;
        if (!profile(index)) {
            return false;
        }
    }
    return true;
}

bool OpticalFont::profile(size_t glyph) {
    const size_t canvas = 3u * metrics_.width;
    const int origin = metrics_.width;
    const u8* const mask = (const u8*)(masks_.data()) + glyphs_[glyph].mask;
    int inkLeft = (int)(canvas);
    int inkRight = 0;
    for (u16 y = 0; y < metrics_.height; ++y) {
        const u8* const row = mask + (size_t)(y)*canvas;
        double mass = 0.0;
        for (size_t x = 0; x < canvas; ++x) {
            mass += row[x];
            if (row[x] >= 16) {
                inkLeft = (int)(x) < inkLeft ? (int)(x) : inkLeft;
                inkRight = (int)(x + 1) > inkRight ? (int)(x + 1) : inkRight;
            }
        }
        if (mass == 0.0) {
            continue;
        }
        RowProfile& result = profiles_.mut(glyph * metrics_.height + y);
        result.left = quantile(row, canvas, mass, 0.05) - origin;
        result.right = quantile(row, canvas, mass, 0.95) - origin;
        result.mass = mass;
    }
    if (inkRight <= inkLeft) {
        return false;
    }
    glyphs_.mut(glyph).left = inkLeft - origin;
    glyphs_.mut(glyph).right = inkRight - origin;
    return true;
}

double OpticalFont::pairBearing(size_t left, size_t right) const {
    const double tau = metrics_.width > 6 ? metrics_.width / 6.0 : 1.0;
    double largest = -HUGE_VAL;
    double weights = 0.0;
    for (u16 y = 0; y < metrics_.height; ++y) {
        const RowProfile& a = profiles_[left * metrics_.height + y];
        const RowProfile& b = profiles_[right * metrics_.height + y];
        if (a.mass == 0.0 || b.mass == 0.0) {
            continue;
        }
        const double exponent = -(b.left - a.right) / tau;
        largest = exponent > largest ? exponent : largest;
        weights += sqrt(a.mass * b.mass);
    }
    if (weights == 0.0) {
        return glyphs_[right].left - glyphs_[left].right;
    }
    double scaled = 0.0;
    for (u16 y = 0; y < metrics_.height; ++y) {
        const RowProfile& a = profiles_[left * metrics_.height + y];
        const RowProfile& b = profiles_[right * metrics_.height + y];
        if (a.mass != 0.0 && b.mass != 0.0) {
            scaled += sqrt(a.mass * b.mass) * exp(-(b.left - a.right) / tau - largest);
        }
    }
    return -tau * (largest + log(scaled / weights));
}

double OpticalFont::edgeBearing(size_t glyph, bool left) const {
    const double tau = metrics_.width > 6 ? metrics_.width / 6.0 : 1.0;
    double largest = -HUGE_VAL;
    double weights = 0.0;
    for (u16 y = 0; y < metrics_.height; ++y) {
        const RowProfile& row = profiles_[glyph * metrics_.height + y];
        if (row.mass == 0.0) {
            continue;
        }
        const double gap = left ? row.left : -row.right;
        const double exponent = -gap / tau;
        largest = exponent > largest ? exponent : largest;
        weights += row.mass;
    }
    if (weights == 0.0) {
        return left ? glyphs_[glyph].left : -glyphs_[glyph].right;
    }
    double scaled = 0.0;
    for (u16 y = 0; y < metrics_.height; ++y) {
        const RowProfile& row = profiles_[glyph * metrics_.height + y];
        if (row.mass != 0.0) {
            const double gap = left ? row.left : -row.right;
            scaled += row.mass * exp(-gap / tau - largest);
        }
    }
    return -tau * (largest + log(scaled / weights));
}

bool OpticalFont::collides(size_t left, size_t right, int distance) const {
    const int canvas = 3 * metrics_.width;
    const u8* const a = (const u8*)(masks_.data()) + glyphs_[left].mask;
    const u8* const b = (const u8*)(masks_.data()) + glyphs_[right].mask;
    const int begin = distance > 0 ? distance : 0;
    const int end = distance + canvas < canvas ? distance + canvas : canvas;
    for (u16 y = 0; y < metrics_.height; ++y) {
        const u8* const rowA = a + (size_t)(y)*canvas;
        const u8* const rowB = b + (size_t)(y)*canvas;
        for (int x = begin; x < end; ++x) {
            if (rowA[x] >= 32 && rowB[x - distance] >= 32) {
                return true;
            }
        }
    }
    return false;
}

int OpticalFont::minimumDistance(size_t left, size_t right) const {
    const int limit = 3 * metrics_.width;
    int lastCollision = -1;
    for (int distance = 0; distance <= limit; ++distance) {
        if (collides(left, right, distance)) {
            lastCollision = distance;
        }
    }
    return lastCollision + 1;
}

bool OpticalFont::place(size_t count, int width) {
    gaps_.zero(count + 1);
    origins_.zero(count);
    gaps_.mut(0).bearing = edgeBearing(0, true);
    gaps_.mut(0).coefficient = 0.5;
    gaps_.mut(0).minimum = -glyphs_[0].left;
    for (size_t index = 1; index < count; ++index) {
        OpticalGap& gap = gaps_.mut(index);
        gap.bearing = pairBearing(index - 1, index);
        gap.coefficient = 1.0;
        gap.minimum = minimumDistance(index - 1, index);
    }
    gaps_.mut(count).bearing = edgeBearing(count - 1, false);
    gaps_.mut(count).coefficient = 0.5;
    gaps_.mut(count).minimum = glyphs_[count - 1].right;
    int remaining = width;
    double clearance = 0.0;
    for (;;) {
        double coefficients = 0.0;
        double bearings = 0.0;
        for (size_t index = 0; index < gaps_.length(); ++index) {
            const OpticalGap& gap = gaps_[index];
            if (!gap.fixed) {
                coefficients += gap.coefficient;
                bearings += gap.bearing;
            }
        }
        if (coefficients == 0.0) {
            if (remaining != 0) {
                return false;
            }
            break;
        }
        clearance = (remaining + bearings) / coefficients;
        bool constrained = false;
        for (size_t index = 0; index < gaps_.length(); ++index) {
            OpticalGap& gap = gaps_.mut(index);
            if (gap.fixed) {
                continue;
            }
            gap.distance = gap.coefficient * clearance - gap.bearing;
            if (gap.distance < gap.minimum) {
                gap.fixed = true;
                gap.distance = gap.minimum;
                remaining -= gap.minimum;
                constrained = true;
                if (remaining < 0) {
                    return false;
                }
            }
        }
        if (!constrained) {
            break;
        }
    }
    int used = 0;
    for (size_t index = 0; index < gaps_.length(); ++index) {
        OpticalGap& gap = gaps_.mut(index);
        gap.pixels = gap.fixed ? gap.minimum : (int)(floor(gap.distance + 0.000001));
        gap.pixels = gap.pixels < gap.minimum ? gap.minimum : gap.pixels;
        used += gap.pixels;
    }
    while (used < width) {
        size_t best = 0;
        double bestCost = HUGE_VAL;
        for (size_t index = 0; index < gaps_.length(); ++index) {
            const OpticalGap& gap = gaps_[index];
            const double target = gap.coefficient * clearance;
            const double error = gap.pixels + gap.bearing - target;
            const double cost = (error + 1.0) * (error + 1.0) - error * error;
            if (cost < bestCost) {
                best = index;
                bestCost = cost;
            }
        }
        ++gaps_.mut(best).pixels;
        ++used;
    }
    if (used != width) {
        return false;
    }
    int position = gaps_[0].pixels;
    origins_.mut(0) = position;
    for (size_t index = 1; index < count; ++index) {
        position += gaps_[index].pixels;
        origins_.mut(index) = position;
    }
    return true;
}

void OpticalFont::draw(size_t count, u16 cells, u8* out) const {
    const int canvas = 3 * metrics_.width;
    const int stride = cells * metrics_.width;
    const int origin = metrics_.width;
    for (size_t glyph = 0; glyph < count; ++glyph) {
        const u8* const mask = (const u8*)(masks_.data()) + glyphs_[glyph].mask;
        const int destination = origins_[glyph] - origin;
        for (u16 y = 0; y < metrics_.height; ++y) {
            const u8* const source = mask + (size_t)(y)*canvas;
            u8* const row = out + (size_t)(y)*stride;
            for (int x = 0; x < canvas; ++x) {
                const int target = destination + x;
                if (target >= 0 && target < stride && source[x] > row[target]) {
                    row[target] = source[x];
                }
            }
        }
    }
}

OpticalFontRenderer::OpticalFontRenderer(Composer& composer, FontRenderer* base)
    : composer_(composer)
    , base_(base)
{
}

Font* OpticalFontRenderer::render(ObjPool& owner, IntrusivePtr<FontFace> face, u16 pixels, FontKind kind, FontMetrics& metrics) {
    Font* const font = base_->render(owner, face, pixels, kind, metrics);
    return font != nullptr ? owner.make<OpticalFont>(composer_, font, metrics) : nullptr;
}

FontRenderer* createOpticalFontRenderer(Composer& composer, FontRenderer* base) {
    return composer.pool->make<OpticalFontRenderer>(composer, base);
}
