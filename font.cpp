/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "font.h"

#include "composer.h"
#include "grapheme.h"
#include "options.h"
#include "utf8.h"

#include <std/lib/buffer.h>
#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/sys/crt.h>
#include <std/sys/throw.h>
#include <std/typ/support.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <errno.h>

namespace stl {}

using namespace stl;

namespace {
    class FontImpl final: public Font {
    public:
        FontImpl(StringView filename, FontKind kind, FontMetrics& metrics);
        ~FontImpl() noexcept;

        FontGlyph glyph(u32 id) override;

    private:
        void configure();
        void configureFixed();
        void configureScaled();
        bool accepts(u32 id) const;
        bool rasterize(FT_UInt glyphIndex);
        void close() noexcept;
        [[noreturn]] void fail(StringView message);
        [[noreturn]] void fail(StringBuilder&& message);

        FT_Library library_ = nullptr;
        FT_Face face_ = nullptr;
        FontKind kind_;
        FontMetrics metrics_;
        Buffer bitmap_;
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
}

FontImpl::FontImpl(StringView filename, FontKind kind, FontMetrics& metrics)
    : kind_(kind)
    , metrics_(metrics)
{
    if (FT_Init_FreeType(&library_)) {
        fail(StringView(u8"could not initialize FreeType"));
    }

    Buffer filenameBuffer(filename);
    if (FT_New_Face(library_, filenameBuffer.cStr(), 0, &face_)) {
        close();
        fail(StringBuilder() << StringView(u8"failed to open font ") << filename);
    }

    try {
        configure();
        bitmap_.grow((size_t)(metrics_.width) * metrics_.height);
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
    if (face_ != nullptr) {
        FT_Done_Face(face_);
        face_ = nullptr;
    }
    if (library_ != nullptr) {
        FT_Done_FreeType(library_);
        library_ = nullptr;
    }
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
        const int difference = absolute((int)(opts.fontsize) - face_->available_sizes[index].height);
        if (difference < bestDifference) {
            bestIndex = index;
            bestDifference = difference;
        }
    }
    if (bestIndex < 0) {
        fail(StringView(u8"font advertises no usable fixed size"));
    }
    if (bestDifference > 1 && face_->units_per_EM > 0) {
        configureScaled();
        return;
    }

    const FT_Bitmap_Size& size = face_->available_sizes[bestIndex];
    if (kind_ == FontKind::Primary) {
        metrics_.width = size.width;
        metrics_.height = size.height;
        metrics_.baseline = 0;
    } else if (metrics_.width != size.width || metrics_.height != size.height) {
        fail(StringBuilder() << StringView(u8"font cell mismatch: expected ") << metrics_.width << StringView(u8"x") << metrics_.height << StringView(u8", got ") << size.width << StringView(u8"x") << size.height);
    }

    if (FT_Set_Pixel_Sizes(face_, metrics_.width, metrics_.height)) {
        fail(StringView(u8"could not select fixed font size"));
    }
    if (kind_ != FontKind::Overlay && face_->height != 0) {
        metrics_.baseline = rounded(metrics_.height * (double)(face_->ascender) / face_->height);
    }
}

void FontImpl::configureScaled() {
    if (FT_Set_Pixel_Sizes(face_, opts.fontsize, opts.fontsize)) {
        fail(StringView(u8"could not select scalable font size"));
    }
    if (face_->units_per_EM == 0 || face_->max_advance_width == 0 || face_->height == 0) {
        fail(StringView(u8"font has unusable scalable metrics"));
    }

    const double width = opts.fontsize * (double)(face_->max_advance_width) / face_->units_per_EM;
    const double height = width * face_->height / face_->max_advance_width + 1;
    const FontMetrics actual{
        .width = rounded(width),
        .height = rounded(height),
        .baseline = rounded(height * face_->ascender / face_->height),
    };
    if (kind_ != FontKind::Primary && (metrics_.width != actual.width || metrics_.height != actual.height)) {
        fail(StringBuilder() << StringView(u8"font cell mismatch: expected ") << metrics_.width << StringView(u8"x") << metrics_.height << StringView(u8", got ") << actual.width << StringView(u8"x") << actual.height);
    }
    if (kind_ == FontKind::Overlay && metrics_.baseline != actual.baseline) {
        fail(StringBuilder() << StringView(u8"font baseline mismatch: expected ") << metrics_.baseline << StringView(u8", got ") << actual.baseline);
    }
    if (kind_ == FontKind::Primary) {
        metrics_ = actual;
    } else if (kind_ == FontKind::DoubleWidth) {
        metrics_.baseline = actual.baseline;
    }
}

bool FontImpl::accepts(u32 id) const {
    if (id == Missing_Glyph_Marker || id == Unicode_Replacement_Character) {
        return true;
    }
    const int width = codepointWidth(id);
    return kind_ == FontKind::DoubleWidth ? width == 2 : width < 2;
}

bool FontImpl::rasterize(FT_UInt glyphIndex) {
    if (FT_Load_Glyph(face_, glyphIndex, FT_LOAD_RENDER)) {
        return false;
    }

    bitmap_.zero((size_t)(metrics_.width) * metrics_.height);
    const FT_Bitmap& source = face_->glyph->bitmap;
    const int sourceWidth = source.width;
    const int sourceHeight = source.rows;
    const int destinationX = maximum(0, face_->glyph->bitmap_left);
    const int destinationY = maximum(0, metrics_.baseline > 0 ? metrics_.baseline - face_->glyph->bitmap_top : 0);
    const int sourceX = maximum(0, -face_->glyph->bitmap_left);
    const int sourceY = maximum(0, metrics_.baseline > 0 ? face_->glyph->bitmap_top - metrics_.baseline : 0);
    const int copyWidth = minimum(sourceWidth - sourceX, (int)(metrics_.width) - destinationX);
    const int copyHeight = minimum(sourceHeight - sourceY, (int)(metrics_.height) - destinationY);
    if (copyWidth <= 0 || copyHeight <= 0) {
        return true;
    }

    const int pitch = source.pitch;
    const int rowStride = pitch < 0 ? -pitch : pitch;
    u8* destination = (u8*)(bitmap_.mutData());
    for (int row = 0; row < copyHeight; ++row) {
        const int sourceRow = sourceY + row;
        const int storedRow = pitch < 0 ? sourceHeight - sourceRow - 1 : sourceRow;
        const unsigned char* sourcePixels = source.buffer + storedRow * rowStride;
        u8* destinationPixels = destination + (destinationY + row) * metrics_.width + destinationX;
        if (source.pixel_mode == FT_PIXEL_MODE_GRAY) {
            memCpy(destinationPixels, sourcePixels + sourceX, copyWidth);
        } else if (source.pixel_mode == FT_PIXEL_MODE_MONO) {
            for (int column = 0; column < copyWidth; ++column) {
                const int sourceColumn = sourceX + column;
                destinationPixels[column] = sourcePixels[sourceColumn >> 3] & (0x80 >> (sourceColumn & 7)) ? 0xff : 0;
            }
        } else {
            return false;
        }
    }
    return true;
}

FontGlyph FontImpl::glyph(u32 id) {
    if (!accepts(id)) {
        return {};
    }
    const FT_UInt glyphIndex = id == Missing_Glyph_Marker ? 0 : FT_Get_Char_Index(face_, id);
    if (id != Missing_Glyph_Marker && glyphIndex == 0) {
        return {};
    }
    if (!rasterize(glyphIndex)) {
        return {};
    }
    return {
        .data = bitmap_.data(),
        .len = bitmap_.used(),
    };
}

Font* Font::create(Composer& composer, StringView filename, FontKind kind, FontMetrics& metrics) {
    return composer.pool->make<FontImpl>(filename, kind, metrics);
}
