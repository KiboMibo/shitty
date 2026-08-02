/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "font_freetype.h"

#include "composer.h"
#include "font_face.h"
#include "grapheme.h"
#include "options.h"
#include "utf8.h"

#include <std/ios/sys.h>

#include <std/lib/buffer.h>
#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/sys/crt.h>
#include <std/sys/throw.h>
#include <std/typ/support.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SYNTHESIS_H
#include <hb-ft.h>
#include <hb.h>

#include <errno.h>
#include <math.h>

using namespace stl;

namespace {
    struct FontImpl final: public Font {
        FontImpl(IntrusivePtr<FontFace> source, u16 size, FontKind kind, FontMetrics& metrics, FontStyle synthetic);
        ~FontImpl() noexcept;

        FontGlyph glyph(const u32* codepoints, size_t count, u16 cells) override;
        bool covers(u32 codepoint) override;
        Font* synthesize(ObjPool& owner, FontStyle style) override;

        struct FitMeasure {
            int advance = 0;
            int ascent = 0;
            int descent = 0;
        };

        void configure();
        void configureFixed();
        void configureScaled();
        u16 representativeAdvance();
        u32 fitRepresentative(u16 cells) const;
        FitMeasure measureAt(u16 pixelSize, u32 representative);
        u16 fitCells(u16 cells);
        bool applyCells(u16 cells);
        bool loadGlyph(FT_UInt glyph, bool color, bool render);
        bool rasterize(const u32* codepoints, size_t count);
        bool rasterizeMask(const hb_glyph_info_t* glyphs, const hb_glyph_position_t* positions, unsigned count);
        bool rasterizeColor(const hb_glyph_info_t* glyphs, const hb_glyph_position_t* positions, unsigned count);
        void drawMask(const FT_Bitmap& source, int destinationX, int destinationY);
        void drawColor(const FT_Bitmap& source, int destinationX, int destinationY, int sourceWidth, int sourceHeight);
        void scaleColor(int sourceWidth, int sourceHeight);
        void close() noexcept;
        [[noreturn]] void fail(StringView message);
        [[noreturn]] void fail(StringBuilder&& message);

        // Keeps the mapped bytes alive for as long as the FT face uses them.
        IntrusivePtr<FontFace> source_;
        FT_Library library_ = nullptr;
        FT_Face face_ = nullptr;
        hb_font_t* harfbuzz_ = nullptr;
        hb_buffer_t* shape_ = nullptr;
        u16 size_;
        FontKind kind_;
        FontMetrics metrics_;
        u16 canvasWidth_ = 0;
        u16 fittedSize_[3] = {0, 0, 0};
        bool syntheticBold_ = false;
        bool syntheticItalic_ = false;
        bool fitLogged_ = false;
        bool hasColor_ = false;
        bool glyphColor_ = false;
        Buffer bitmap_;
        Buffer sourceBitmap_;
    };

    struct FreeTypeRenderer final: public FontRenderer {
        Font* render(ObjPool& owner, IntrusivePtr<FontFace> face, u16 pixels, FontKind kind, FontMetrics& metrics) override;
    };

    int absolute(int value) {
        return value < 0 ? -value : value;
    }

    int maximum(int left, int right) {
        return left > right ? left : right;
    }

    int minimum(int left, int right) {
        return left < right ? left : right;
    }

    u16 rounded(double value) {
        return (u16)(value + 0.5);
    }

    int pixels(hb_position_t value) {
        return value >= 0 ? (value + 32) / 64 : -((-value + 32) / 64);
    }

    // One process-wide library instead of one per font: FT_Library only
    // holds allocator and module state, and the faces keep it alive for
    // the process lifetime anyway.
    FT_Library sharedFreeType() {
        static FT_Library library = [] {
            FT_Library value = nullptr;
            if (FT_Init_FreeType(&value) != 0) {
                value = nullptr;
            }
            return value;
        }();
        return library;
    }
}

FontImpl::FontImpl(IntrusivePtr<FontFace> source, u16 size, FontKind kind, FontMetrics& metrics, FontStyle synthetic)
    : source_(source)
    , size_(size)
    , kind_(kind)
    , metrics_(metrics)
    , syntheticBold_(synthetic == FontStyle::Bold || synthetic == FontStyle::BoldItalic)
    , syntheticItalic_(synthetic == FontStyle::Italic || synthetic == FontStyle::BoldItalic)
{
    library_ = sharedFreeType();
    if (library_ == nullptr) {
        fail(StringView(u8"could not initialize FreeType"));
    }

    if (FT_New_Memory_Face(library_, (const FT_Byte*)(source_->data()), (FT_Long)(source_->size()), source_->faceIndex(), &face_)) {
        close();
        fail(StringBuilder() << StringView(u8"failed to open font face ") << source_->faceIndex());
    }

    try {
        hasColor_ = FT_HAS_COLOR(face_);
        configure();
        harfbuzz_ = hb_ft_font_create_referenced(face_);
        shape_ = hb_buffer_create();
        if (harfbuzz_ == nullptr || shape_ == nullptr || !hb_buffer_allocation_successful(shape_)) {
            fail(StringView(u8"could not initialize font shaping"));
        }
    } catch (...) {
        close();
        throw;
    }

    metrics = metrics_;
}

FontImpl::~FontImpl() noexcept {
    close();
}

void FontImpl::close() noexcept {
    if (shape_ != nullptr) {
        hb_buffer_destroy(shape_);
        shape_ = nullptr;
    }
    if (harfbuzz_ != nullptr) {
        hb_font_destroy(harfbuzz_);
        harfbuzz_ = nullptr;
    }
    if (face_ != nullptr) {
        FT_Done_Face(face_);
        face_ = nullptr;
    }
    // The shared FT_Library lives for the process.
    library_ = nullptr;
}

void FontImpl::fail(StringView message) {
    Errno(EINVAL).raise(Buffer(message));
}

void FontImpl::fail(StringBuilder&& message) {
    Errno(EINVAL).raise(move(message));
}

void FontImpl::configure() {
    if (face_->num_fixed_sizes > 0) {
        configureFixed();
    } else {
        configureScaled();
    }
    if (metrics_.width == 0 || metrics_.height == 0) {
        fail(StringView(u8"font has zero-sized glyph cells"));
    }
}

void FontImpl::configureFixed() {
    int bestIndex = -1;
    int bestDifference = 0x7fffffff;
    for (int index = 0; index < face_->num_fixed_sizes; ++index) {
        const FT_Bitmap_Size& size = face_->available_sizes[index];
        const int pixels = size.y_ppem > 0 ? maximum(1, rounded(size.y_ppem / 64.0)) : size.height;
        const int difference = absolute((int)(size_)-pixels);
        if (difference < bestDifference) {
            bestIndex = index;
            bestDifference = difference;
        }
    }
    if (bestIndex < 0) {
        fail(StringView(u8"font advertises no usable fixed size"));
    }
    if (bestDifference > 1 && face_->units_per_EM > 0 && !hasColor_) {
        configureScaled();
        return;
    }
    if (FT_Select_Size(face_, bestIndex)) {
        fail(StringView(u8"could not select fixed font size"));
    }

    const FT_Bitmap_Size& size = face_->available_sizes[bestIndex];
    FontMetrics actual{
        .width = (u16)(size.width),
        .height = (u16)(size.height),
    };
    if (kind_ == FontKind::Primary) {
        const int pixels = size.y_ppem > 0 ? maximum(1, rounded(size.y_ppem / 64.0)) : size.height;
        const double scale = hasColor_ ? size_ / (double)(pixels) : 1;
        actual.width = rounded(size.width * scale);
        actual.height = rounded(size.height * scale);
    }
    if (face_->height != 0) {
        actual.baseline = rounded(actual.height * (double)(face_->ascender) / face_->height);
    }
    if (kind_ == FontKind::Primary) {
        metrics_ = actual;
        return;
    }
    if (kind_ == FontKind::Overlay) {
        if (metrics_.height != actual.height) {
            fail(StringBuilder() << StringView(u8"font cell height mismatch: expected ") << metrics_.height << StringView(u8", got ") << actual.height);
        }
        if (metrics_.baseline != actual.baseline) {
            fail(StringBuilder() << StringView(u8"font baseline mismatch: expected ") << metrics_.baseline << StringView(u8", got ") << actual.baseline);
        }
        return;
    }
    if (hasColor_) {
        if (face_->height != 0) {
            metrics_.baseline = rounded(metrics_.height * (double)(face_->ascender) / face_->height);
        }
        return;
    }
    // A fallback mask strike is drawn at its own size and clipped into the
    // imposed cell box; there is nothing to validate.
}

u16 FontImpl::representativeAdvance() {
    const FT_UInt glyph = FT_Get_Char_Index(face_, 'M');
    if (glyph != 0 && !FT_Load_Glyph(face_, glyph, FT_LOAD_DEFAULT)) {
        const int advance = pixels(face_->glyph->advance.x);
        if (advance > 0 && advance <= UINT16_MAX) {
            return (u16)(advance);
        }
    }
    return rounded(size_ * (double)(face_->max_advance_width) / face_->units_per_EM);
}

void FontImpl::configureScaled() {
    if (FT_Set_Pixel_Sizes(face_, size_, size_)) {
        fail(StringView(u8"could not select scalable font size"));
    }
    if (face_->units_per_EM == 0 || face_->max_advance_width == 0 || face_->height == 0) {
        fail(StringView(u8"font has unusable scalable metrics"));
    }

    const double height = size_ * (double)(face_->height) / face_->units_per_EM + 1;
    const FontMetrics actual{
        .width = representativeAdvance(),
        .height = rounded(height),
        .baseline = rounded(height * face_->ascender / face_->height),
    };
    if (kind_ == FontKind::Primary) {
        metrics_ = actual;
        return;
    }
    if (kind_ == FontKind::Overlay) {
        if (metrics_.height != actual.height) {
            fail(StringBuilder() << StringView(u8"font cell height mismatch: expected ") << metrics_.height << StringView(u8", got ") << actual.height);
        }
        if (metrics_.baseline != actual.baseline) {
            fail(StringBuilder() << StringView(u8"font baseline mismatch: expected ") << metrics_.baseline << StringView(u8", got ") << actual.baseline);
        }
        return;
    }
    // Fallback faces impose no cell of their own: the effective pixel size
    // is chosen per cell span by fitCells.
}

bool FontImpl::covers(u32 codepoint) {
    return FT_Get_Char_Index(face_, codepoint) != 0;
}

Font* FontImpl::synthesize(ObjPool& owner, FontStyle style) {
    FontMetrics metrics = metrics_;
    try {
        return owner.make<FontImpl>(source_, size_, FontKind::Overlay, metrics, style);
    } catch (Exception&) {
        return nullptr;
    }
}

// Loads without rendering first so a synthetic style can embolden or
// shear the outline, then renders.
bool FontImpl::loadGlyph(FT_UInt glyph, bool color, bool render) {
    FT_Int32 flags = FT_LOAD_DEFAULT;
    if (color) {
        flags |= FT_LOAD_COLOR;
    }
    if (FT_Load_Glyph(face_, glyph, flags)) {
        return false;
    }
    if (syntheticBold_) {
        FT_GlyphSlot_Embolden(face_->glyph);
    }
    if (syntheticItalic_) {
        FT_GlyphSlot_Oblique(face_->glyph);
    }
    if (render && face_->glyph->format != FT_GLYPH_FORMAT_BITMAP && FT_Render_Glyph(face_->glyph, FT_RENDER_MODE_NORMAL)) {
        return false;
    }
    return true;
}

u32 FontImpl::fitRepresentative(u16 cells) const {
    const u32 wide[] = {0x3000, 0x4e00};
    const u32 narrow[] = {'M', '0'};
    for (const u32 codepoint : cells > 1 ? wide : narrow) {
        if (FT_Get_Char_Index(face_, codepoint) != 0) {
            return codepoint;
        }
    }
    return 0;
}

FontImpl::FitMeasure FontImpl::measureAt(u16 pixelSize, u32 representative) {
    FitMeasure result;
    if (FT_Set_Pixel_Sizes(face_, 0, pixelSize)) {
        return result;
    }
    result.ascent = (int)(face_->size->metrics.ascender + 63) / 64;
    result.descent = (int)(-face_->size->metrics.descender + 63) / 64;
    result.advance = (int)(face_->size->metrics.max_advance + 63) / 64;
    if (representative != 0) {
        const FT_UInt glyph = FT_Get_Char_Index(face_, representative);
        if (glyph != 0 && !FT_Load_Glyph(face_, glyph, FT_LOAD_DEFAULT)) {
            const int advance = pixels(face_->glyph->advance.x);
            if (advance > 0) {
                result.advance = advance;
            }
        }
    }
    return result;
}

// Width-anchored fit: start where the representative advance matches the
// target span and let the engine re-render smaller until the vertical
// metrics stay inside the primary cell. The result depends only on the
// face and the span, so it is computed once per span.
u16 FontImpl::fitCells(u16 cells) {
    u16& cached = fittedSize_[cells];
    if (cached != 0) {
        return cached;
    }

    const int targetWidth = cells * metrics_.width;
    const int targetAscent = metrics_.baseline;
    const int targetDescent = metrics_.height - metrics_.baseline;
    const u32 representative = fitRepresentative(cells);
    u16 size = (u16)(maximum(1, metrics_.height));
    FitMeasure fit = measureAt(size, representative);
    if (fit.advance > 0 && fit.advance != targetWidth) {
        size = (u16)(maximum(1, size * targetWidth / fit.advance));
        fit = measureAt(size, representative);
    }
    while (size > 1 && (fit.advance > targetWidth || fit.ascent > targetAscent || fit.descent > targetDescent)) {
        --size;
        fit = measureAt(size, representative);
    }
    if (opts.verbose && !fitLogged_) {
        sysO << StringView(u8"fitted fallback font to ") << size << StringView(u8"px for ") << (u64)(cells) << StringView(u8"-cell glyphs\n");
        fitLogged_ = true;
    }
    cached = size;
    return size;
}

bool FontImpl::applyCells(u16 cells) {
    if (kind_ != FontKind::Fallback || face_->num_fixed_sizes > 0) {
        return true;
    }
    if (FT_Set_Pixel_Sizes(face_, 0, fitCells(cells))) {
        return false;
    }
    hb_ft_font_changed(harfbuzz_);
    return true;
}

void FontImpl::drawMask(const FT_Bitmap& source, int destinationX, int destinationY) {
    const int sourceWidth = source.width;
    const int sourceHeight = source.rows;
    const int sourceX = maximum(0, -destinationX);
    const int sourceY = maximum(0, -destinationY);
    destinationX = maximum(0, destinationX);
    destinationY = maximum(0, destinationY);
    const int copyWidth = minimum(sourceWidth - sourceX, (int)(canvasWidth_)-destinationX);
    const int copyHeight = minimum(sourceHeight - sourceY, (int)(metrics_.height) - destinationY);
    if (copyWidth <= 0 || copyHeight <= 0) {
        return;
    }

    const int pitch = source.pitch;
    const int rowStride = absolute(pitch);
    auto* destination = (u8*)(bitmap_.mutData());
    for (int row = 0; row < copyHeight; ++row) {
        const int sourceRow = sourceY + row;
        const int storedRow = pitch < 0 ? sourceHeight - sourceRow - 1 : sourceRow;
        const u8* sourcePixels = (const u8*)(source.buffer + storedRow * rowStride);
        u8* destinationPixels = destination + (destinationY + row) * canvasWidth_ + destinationX;
        if (source.pixel_mode == FT_PIXEL_MODE_GRAY) {
            for (int column = 0; column < copyWidth; ++column) {
                destinationPixels[column] = maximum(destinationPixels[column], sourcePixels[sourceX + column]);
            }
        } else if (source.pixel_mode == FT_PIXEL_MODE_MONO) {
            for (int column = 0; column < copyWidth; ++column) {
                const int sourceColumn = sourceX + column;
                const u8 coverage = sourcePixels[sourceColumn >> 3] & (0x80 >> (sourceColumn & 7)) ? 0xff : 0;
                destinationPixels[column] = maximum(destinationPixels[column], coverage);
            }
        }
    }
}

bool FontImpl::rasterizeMask(const hb_glyph_info_t* glyphs, const hb_glyph_position_t* positions, unsigned count) {
    bitmap_.zero((size_t)(canvasWidth_)*metrics_.height);
    hb_position_t penX = 0;
    hb_position_t penY = 0;
    if (kind_ == FontKind::Fallback) {
        // A fitted glyph can come out narrower than its span; center it.
        hb_position_t total = 0;
        for (unsigned index = 0; index < count; ++index) {
            total += positions[index].x_advance;
        }
        const hb_position_t canvas = (hb_position_t)(canvasWidth_) << 6;
        if (total > 0 && total < canvas) {
            penX = (canvas - total) / 2;
        }
    }
    for (unsigned index = 0; index < count; ++index) {
        if (!loadGlyph(glyphs[index].codepoint, false, true)) {
            return false;
        }
        const int destinationX = pixels(penX + positions[index].x_offset) + face_->glyph->bitmap_left;
        const int destinationY = metrics_.baseline - pixels(penY + positions[index].y_offset) - face_->glyph->bitmap_top;
        const FT_Bitmap& source = face_->glyph->bitmap;
        if (source.pixel_mode != FT_PIXEL_MODE_GRAY && source.pixel_mode != FT_PIXEL_MODE_MONO) {
            return false;
        }
        drawMask(source, destinationX, destinationY);
        penX += positions[index].x_advance;
        penY += positions[index].y_advance;
    }
    return true;
}

void FontImpl::drawColor(const FT_Bitmap& source, int destinationX, int destinationY, int sourceWidth, int sourceHeight) {
    const int pitch = source.pitch;
    const int rowStride = absolute(pitch);
    auto* destination = (u8*)(sourceBitmap_.mutData());
    for (int row = 0; row < source.rows; ++row) {
        const int storedRow = pitch < 0 ? source.rows - row - 1 : row;
        const u8* sourcePixels = (const u8*)(source.buffer + storedRow * rowStride);
        for (int column = 0; column < source.width; ++column) {
            const int x = destinationX + column;
            const int y = destinationY + row;
            if (x < 0 || y < 0 || x >= sourceWidth || y >= sourceHeight) {
                continue;
            }
            u8 red = 0;
            u8 green = 0;
            u8 blue = 0;
            u8 alpha = 0;
            if (source.pixel_mode == FT_PIXEL_MODE_BGRA) {
                const u8* pixel = sourcePixels + 4 * column;
                blue = pixel[0];
                green = pixel[1];
                red = pixel[2];
                alpha = pixel[3];
            } else if (source.pixel_mode == FT_PIXEL_MODE_GRAY) {
                alpha = sourcePixels[column];
                red = alpha;
                green = alpha;
                blue = alpha;
            } else if (source.pixel_mode == FT_PIXEL_MODE_MONO) {
                alpha = sourcePixels[column >> 3] & (0x80 >> (column & 7)) ? 0xff : 0;
                red = alpha;
                green = alpha;
                blue = alpha;
            } else {
                continue;
            }

            u8* target = destination + 4 * ((size_t)(y)*sourceWidth + x);
            const unsigned inverse = 255 - alpha;
            target[0] = (u8)(red + (unsigned)(target[0]) * inverse / 255);
            target[1] = (u8)(green + (unsigned)(target[1]) * inverse / 255);
            target[2] = (u8)(blue + (unsigned)(target[2]) * inverse / 255);
            target[3] = (u8)(alpha + (unsigned)(target[3]) * inverse / 255);
        }
    }
}

void FontImpl::scaleColor(int sourceWidth, int sourceHeight) {
    bitmap_.zero((size_t)(canvasWidth_)*metrics_.height * 4);
    double scale = minimum(canvasWidth_, metrics_.height) / (double)(maximum(sourceWidth, sourceHeight));
    if (scale > 1) {
        scale = 1;
    }
    const int targetWidth = maximum(1, rounded(sourceWidth * scale));
    const int targetHeight = maximum(1, rounded(sourceHeight * scale));
    const int originX = ((int)(canvasWidth_)-targetWidth) / 2;
    const int originY = ((int)(metrics_.height) - targetHeight) / 2;
    const auto* source = (const u8*)(sourceBitmap_.data());
    auto* destination = (u8*)(bitmap_.mutData());
    for (int y = 0; y < targetHeight; ++y) {
        const double sourceY = (y + 0.5) / scale - 0.5;
        const int firstY = maximum(0, minimum(sourceHeight - 1, (int)(floor(sourceY))));
        const int secondY = minimum(sourceHeight - 1, firstY + 1);
        const double fractionY = sourceY - floor(sourceY);
        for (int x = 0; x < targetWidth; ++x) {
            const double sourceX = (x + 0.5) / scale - 0.5;
            const int firstX = maximum(0, minimum(sourceWidth - 1, (int)(floor(sourceX))));
            const int secondX = minimum(sourceWidth - 1, firstX + 1);
            const double fractionX = sourceX - floor(sourceX);
            const u8* topLeft = source + 4 * ((size_t)(firstY)*sourceWidth + firstX);
            const u8* topRight = source + 4 * ((size_t)(firstY)*sourceWidth + secondX);
            const u8* bottomLeft = source + 4 * ((size_t)(secondY)*sourceWidth + firstX);
            const u8* bottomRight = source + 4 * ((size_t)(secondY)*sourceWidth + secondX);
            u8* target = destination + 4 * ((size_t)(originY + y) * canvasWidth_ + originX + x);
            for (int channel = 0; channel < 4; ++channel) {
                const double top = topLeft[channel] * (1 - fractionX) + topRight[channel] * fractionX;
                const double bottom = bottomLeft[channel] * (1 - fractionX) + bottomRight[channel] * fractionX;
                target[channel] = (u8)(top * (1 - fractionY) + bottom * fractionY + 0.5);
            }
        }
    }
}

bool FontImpl::rasterizeColor(const hb_glyph_info_t* glyphs, const hb_glyph_position_t* positions, unsigned count) {
    hb_position_t penX = 0;
    hb_position_t penY = 0;
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    bool haveBounds = false;
    for (unsigned index = 0; index < count; ++index) {
        if (!loadGlyph(glyphs[index].codepoint, true, true)) {
            return false;
        }
        const int glyphLeft = pixels(penX + positions[index].x_offset) + face_->glyph->bitmap_left;
        const int glyphTop = -pixels(penY + positions[index].y_offset) - face_->glyph->bitmap_top;
        const int glyphRight = glyphLeft + face_->glyph->bitmap.width;
        const int glyphBottom = glyphTop + face_->glyph->bitmap.rows;
        if (!haveBounds) {
            left = glyphLeft;
            top = glyphTop;
            right = glyphRight;
            bottom = glyphBottom;
            haveBounds = true;
        } else {
            left = minimum(left, glyphLeft);
            top = minimum(top, glyphTop);
            right = maximum(right, glyphRight);
            bottom = maximum(bottom, glyphBottom);
        }
        penX += positions[index].x_advance;
        penY += positions[index].y_advance;
    }
    const int sourceWidth = right - left;
    const int sourceHeight = bottom - top;
    if (!haveBounds || sourceWidth <= 0 || sourceHeight <= 0) {
        return false;
    }

    sourceBitmap_.zero((size_t)(sourceWidth)*sourceHeight * 4);
    penX = 0;
    penY = 0;
    for (unsigned index = 0; index < count; ++index) {
        if (!loadGlyph(glyphs[index].codepoint, true, true)) {
            return false;
        }
        const int glyphLeft = pixels(penX + positions[index].x_offset) + face_->glyph->bitmap_left - left;
        const int glyphTop = -pixels(penY + positions[index].y_offset) - face_->glyph->bitmap_top - top;
        drawColor(face_->glyph->bitmap, glyphLeft, glyphTop, sourceWidth, sourceHeight);
        penX += positions[index].x_advance;
        penY += positions[index].y_advance;
    }
    scaleColor(sourceWidth, sourceHeight);
    return true;
}

bool FontImpl::rasterize(const u32* codepoints, size_t count) {
    hb_glyph_info_t missing{};
    hb_glyph_position_t missingPosition{};
    if (count == 1 && codepoints[0] == Missing_Glyph_Marker) {
        if (!loadGlyph(0, true, true)) {
            return false;
        }
        glyphColor_ = face_->glyph->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA;
        return glyphColor_ ? rasterizeColor(&missing, &missingPosition, 1) : rasterizeMask(&missing, &missingPosition, 1);
    }

    hb_buffer_clear_contents(shape_);
    hb_buffer_add_codepoints(shape_, (const hb_codepoint_t*)(codepoints), count, 0, count);
    hb_buffer_guess_segment_properties(shape_);
    hb_shape(harfbuzz_, shape_, nullptr, 0);
    unsigned glyphCount = 0;
    const hb_glyph_info_t* glyphs = hb_buffer_get_glyph_infos(shape_, &glyphCount);
    const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(shape_, &glyphCount);
    if (glyphCount == 0) {
        return false;
    }
    for (unsigned index = 0; index < glyphCount; ++index) {
        if (glyphs[index].codepoint == 0) {
            return false;
        }
        if (!loadGlyph(glyphs[index].codepoint, true, true)) {
            return false;
        }
        if (face_->glyph->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA) {
            glyphColor_ = true;
        }
    }
    return glyphColor_ ? rasterizeColor(glyphs, positions, glyphCount) : rasterizeMask(glyphs, positions, glyphCount);
}

FontGlyph FontImpl::glyph(const u32* codepoints, size_t count, u16 cells) {
    glyphColor_ = false;
    if (count == 0 || cells == 0) {
        return {};
    }
    cells = (u16)(minimum(cells, 2));
    canvasWidth_ = (u16)(cells * metrics_.width);
    if (!applyCells(cells) || !rasterize(codepoints, count)) {
        return {};
    }
    return {
        .data = bitmap_.data(),
        .len = bitmap_.used(),
        .color = glyphColor_,
    };
}

Font* FreeTypeRenderer::render(ObjPool& owner, IntrusivePtr<FontFace> face, u16 pixels, FontKind kind, FontMetrics& metrics) {
    return owner.make<FontImpl>(face, pixels, kind, metrics, FontStyle::Regular);
}

FontRenderer* createFreeTypeFontRenderer(Composer& composer) {
    return composer.pool->make<FreeTypeRenderer>();
}
