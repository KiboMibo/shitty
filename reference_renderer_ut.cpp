/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "reference_renderer.h"

#include "cell_extra_store.h"
#include "composer.h"
#include "font_pack.h"
#include "options.h"
#include "vterm.h"

#include <std/mem/obj_pool.h>
#include <std/tst/ut.h>

using namespace stl;

namespace {
    struct FakeFontpack final: public Fontpack {
        u16 getPx() const override;
        u16 getPy() const override;
        bool hasBold() const override;
        bool hasItalic() const override;
        bool hasBoldItalic() const override;
        bool hasDoubleWidth() const override;
        FontGlyph glyph(const u32* codepoints, size_t count, FontStyle style, bool doubleWidth) override;

        u8 bitmap[128]{};
        size_t bitmapLength = 0;
        bool colorGlyph = false;
        bool doubleWidthAvailable = true;
        u32 received[8]{};
        size_t receivedCount = 0;
        FontStyle receivedStyle = FontStyle::Regular;
        bool receivedDoubleWidth = false;
    };

    void configure(Composer& composer, FakeFontpack& fonts, u16 columns, u16 rows, u16 glyphWidth, u16 glyphHeight) {
        composer.fonts = &fonts;
        composer.setGlyphSize(glyphWidth, glyphHeight);
        CellExtraStore::create(composer, (size_t)(columns)*rows);
        composer.resize((u16)(columns * glyphWidth + 2 * opts.border), (u16)(rows * glyphHeight + 2 * opts.border));
    }

    Color pixel(const ReferenceImage& image, u16 x, u16 y) {
        const size_t index = 3 * ((size_t)(y)*image.width + x);
        return {
            image.pixels[index],
            image.pixels[index + 1],
            image.pixels[index + 2],
        };
    }

    Color cellPixel(const ReferenceImage& image, u16 x, u16 y) {
        return pixel(image, (u16)(opts.border + x), (u16)(opts.border + y));
    }
}

u16 FakeFontpack::getPx() const {
    return 0;
}

u16 FakeFontpack::getPy() const {
    return 0;
}

bool FakeFontpack::hasBold() const {
    return true;
}

bool FakeFontpack::hasItalic() const {
    return true;
}

bool FakeFontpack::hasBoldItalic() const {
    return true;
}

bool FakeFontpack::hasDoubleWidth() const {
    return doubleWidthAvailable;
}

FontGlyph FakeFontpack::glyph(const u32* codepoints, size_t count, FontStyle style, bool doubleWidth) {
    receivedCount = count < 8 ? count : 8;
    for (size_t index = 0; index < receivedCount; ++index) {
        received[index] = codepoints[index];
    }
    receivedStyle = style;
    receivedDoubleWidth = doubleWidth;
    return {
        .data = bitmap,
        .len = bitmapLength,
        .color = colorGlyph,
    };
}

STD_TEST_SUITE(ReferenceRenderer) {
    STD_TEST(RejectsMismatchedCellCount) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        FakeFontpack fonts;
        configure(composer, fonts, 1, 1, 1, 1);
        ReferenceRenderer* renderer = ReferenceRenderer::create(composer);
        TerminalUpdate update;

        const ReferenceImage image = renderer->render(update);

        STD_INSIST(image.pixels == nullptr);
        STD_INSIST(image.length == 0);
    }

    STD_TEST(BlendsMonochromeCoverage) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        FakeFontpack fonts;
        fonts.bitmap[0] = 0;
        fonts.bitmap[1] = 128;
        fonts.bitmap[2] = 255;
        fonts.bitmap[3] = 64;
        fonts.bitmapLength = 4;
        configure(composer, fonts, 1, 1, 2, 2);
        ReferenceRenderer* renderer = ReferenceRenderer::create(composer);
        RenderCell cell;
        cell.fg = {255, 0, 0};
        cell.bg = {0, 0, 255};
        TerminalUpdate update;
        update.cells = &cell;
        update.cellCount = 1;

        const ReferenceImage image = renderer->render(update);

        STD_INSIST((cellPixel(image, 0, 0) == Color{0, 0, 255}));
        STD_INSIST((cellPixel(image, 1, 0) == Color{128, 0, 127}));
        STD_INSIST((cellPixel(image, 0, 1) == Color{255, 0, 0}));
        STD_INSIST((cellPixel(image, 1, 1) == Color{64, 0, 191}));
    }

    STD_TEST(InverseAndScreenReverseCancelEachOther) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        FakeFontpack fonts;
        fonts.bitmap[0] = 255;
        fonts.bitmapLength = 1;
        configure(composer, fonts, 1, 1, 1, 1);
        ReferenceRenderer* renderer = ReferenceRenderer::create(composer);
        RenderCell cell;
        cell.fg = {255, 0, 0};
        cell.bg = {0, 0, 255};
        cell.inverse = true;
        TerminalUpdate update;
        update.cells = &cell;
        update.cellCount = 1;

        ReferenceImage image = renderer->render(update);
        STD_INSIST((cellPixel(image, 0, 0) == Color{0, 0, 255}));

        update.screenReverse = true;
        image = renderer->render(update);
        STD_INSIST((cellPixel(image, 0, 0) == Color{255, 0, 0}));
    }

    STD_TEST(AppliesSelectionColors) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        FakeFontpack fonts;
        fonts.bitmap[0] = 255;
        fonts.bitmap[1] = 0;
        fonts.bitmapLength = 2;
        configure(composer, fonts, 1, 1, 2, 1);
        ReferenceRenderer* renderer = ReferenceRenderer::create(composer);
        RenderCell cell;
        cell.fg = {255, 0, 0};
        cell.bg = {0, 0, 255};
        TerminalUpdate update;
        update.cells = &cell;
        update.cellCount = 1;
        update.snappedSelection = Rect(0, 0);
        update.selectionColorMask = 3;
        update.selectionForeground = {1, 2, 3};
        update.selectionBackground = {4, 5, 6};

        const ReferenceImage image = renderer->render(update);

        STD_INSIST((cellPixel(image, 0, 0) == Color{1, 2, 3}));
        STD_INSIST((cellPixel(image, 1, 0) == Color{4, 5, 6}));
    }

    STD_TEST(CompositesPremultipliedColorGlyph) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        FakeFontpack fonts;
        fonts.bitmap[0] = 100;
        fonts.bitmap[1] = 50;
        fonts.bitmap[2] = 0;
        fonts.bitmap[3] = 128;
        fonts.bitmapLength = 4;
        fonts.colorGlyph = true;
        configure(composer, fonts, 1, 1, 1, 1);
        ReferenceRenderer* renderer = ReferenceRenderer::create(composer);
        RenderCell cell;
        cell.bg = {0, 0, 100};
        TerminalUpdate update;
        update.cells = &cell;
        update.cellCount = 1;

        const ReferenceImage image = renderer->render(update);

        STD_INSIST((cellPixel(image, 0, 0) == Color{100, 50, 49}));
    }

    STD_TEST(PassesStoredGraphemeAndStyleToFontpack) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        FakeFontpack fonts;
        fonts.bitmap[0] = 0;
        fonts.bitmapLength = 1;
        configure(composer, fonts, 1, 1, 1, 1);
        ReferenceRenderer* renderer = ReferenceRenderer::create(composer);
        const u32 codepoints[] = {'a', 0x0301};
        TerminalCell stored{};
        composer.cellExtras->setGrapheme(stored, codepoints, 2);
        RenderCell cell;
        cell.bold = true;
        cell.italic = true;
        cell.grapheme = stored.extraRef();
        TerminalUpdate update;
        update.cells = &cell;
        update.cellCount = 1;

        renderer->render(update);

        STD_INSIST(fonts.receivedCount == 2);
        STD_INSIST(fonts.received[0] == codepoints[0]);
        STD_INSIST(fonts.received[1] == codepoints[1]);
        STD_INSIST(fonts.receivedStyle == FontStyle::BoldItalic);
        STD_INSIST(!fonts.receivedDoubleWidth);
    }
}
