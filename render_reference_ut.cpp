/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "render_reference.h"

#include "cell_extra_store.h"
#include "composer.h"
#include "font_pack.h"
#include "options.h"
#include "vterm.h"

#include <plt/platform_headless.h>

#include <std/mem/obj_pool.h>
#include <std/tst/ut.h>

#include <vector>

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
        composer.setCellExtras(CellExtraStore::create(composer, (size_t)(columns)*rows));
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

    TerminalCell coloredCell(Color foreground, Color background) {
        TerminalCell cell{};
        cell.setForeground(CellColor::direct(foreground));
        cell.setBackground(CellColor::direct(background));
        return cell;
    }

    struct ReferenceFixture {
        explicit ReferenceFixture(Composer& composer)
            : pixels((size_t)(composer.pixelWidth) * composer.pixelHeight * 3) {
            target.pixels = pixels.data();
            target.length = pixels.size();
            target.width = composer.pixelWidth;
            target.height = composer.pixelHeight;
            target.stride = composer.pixelWidth * 3;
            renderer = ReferenceRenderer::create(
                composer,
                {
                    .backend = plt::RenderBackend::Headless,
                    .connection = nullptr,
                    .window = &target,
                }
            );
        }

        ReferenceFixture* operator->() {
            return this;
        }

        ReferenceImage render(const TerminalUpdate& update) {
            return renderer->update(update) ? renderer->image() : ReferenceImage{};
        }

        std::vector<u8> pixels;
        plt::HeadlessRenderTarget target;
        ReferenceRenderer* renderer;
    };
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
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        FakeFontpack fonts;
        configure(composer, fonts, 1, 1, 1, 1);
        ReferenceFixture renderer(composer);
        TerminalColors colors;
        TerminalUpdate update;
        update.colors = &colors;

        const ReferenceImage image = renderer->render(update);

        STD_INSIST(image.pixels == nullptr);
        STD_INSIST(image.length == 0);
    }

    STD_TEST(BlendsMonochromeCoverage) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        FakeFontpack fonts;
        fonts.bitmap[0] = 0;
        fonts.bitmap[1] = 128;
        fonts.bitmap[2] = 255;
        fonts.bitmap[3] = 64;
        fonts.bitmapLength = 4;
        configure(composer, fonts, 1, 1, 2, 2);
        ReferenceFixture renderer(composer);
        TerminalColors colors;
        TerminalCell cell = coloredCell({255, 0, 0}, {0, 0, 255});
        TerminalCellSpan span{0, 1, &cell};
        TerminalUpdate update;
        update.spans = &span;
        update.spanCount = 1;
        update.colors = &colors;

        const ReferenceImage image = renderer->render(update);

        STD_INSIST((cellPixel(image, 0, 0) == Color{0, 0, 255}));
        STD_INSIST((cellPixel(image, 1, 0) == Color{128, 0, 127}));
        STD_INSIST((cellPixel(image, 0, 1) == Color{255, 0, 0}));
        STD_INSIST((cellPixel(image, 1, 1) == Color{64, 0, 191}));
    }

    STD_TEST(InverseAndScreenReverseCancelEachOther) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        FakeFontpack fonts;
        fonts.bitmap[0] = 255;
        fonts.bitmapLength = 1;
        configure(composer, fonts, 1, 1, 1, 1);
        ReferenceFixture renderer(composer);
        TerminalColors colors;
        TerminalCell cell = coloredCell({255, 0, 0}, {0, 0, 255});
        cell.inverse = true;
        TerminalCellSpan span{0, 1, &cell};
        TerminalUpdate update;
        update.spans = &span;
        update.spanCount = 1;
        update.colors = &colors;

        ReferenceImage image = renderer->render(update);
        STD_INSIST((cellPixel(image, 0, 0) == Color{0, 0, 255}));

        update.screenReverse = true;
        image = renderer->render(update);
        STD_INSIST((cellPixel(image, 0, 0) == Color{255, 0, 0}));
    }

    STD_TEST(AppliesSparseUpdatesToRetainedCells) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        FakeFontpack fonts;
        configure(composer, fonts, 2, 1, 1, 1);
        ReferenceFixture renderer(composer);
        TerminalColors colors;
        TerminalCell initial[2]{};
        initial[0].setBackground(CellColor::direct({10, 20, 30}));
        initial[1].setBackground(CellColor::direct({40, 50, 60}));
        TerminalCellSpan span{0, 2, initial};
        TerminalUpdate update;
        update.spans = &span;
        update.spanCount = 1;
        update.colors = &colors;

        ReferenceImage image = renderer->render(update);
        STD_INSIST((cellPixel(image, 0, 0) == Color{10, 20, 30}));
        STD_INSIST((cellPixel(image, 1, 0) == Color{40, 50, 60}));

        TerminalCell changed = initial[1];
        changed.setBackground(CellColor::direct({70, 80, 90}));
        span = {1, 1, &changed};
        image = renderer->render(update);

        STD_INSIST((cellPixel(image, 0, 0) == Color{10, 20, 30}));
        STD_INSIST((cellPixel(image, 1, 0) == Color{70, 80, 90}));
    }

    STD_TEST(AppliesSelectionColors) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        FakeFontpack fonts;
        fonts.bitmap[0] = 255;
        fonts.bitmap[1] = 0;
        fonts.bitmapLength = 2;
        configure(composer, fonts, 1, 1, 2, 1);
        ReferenceFixture renderer(composer);
        TerminalColors colors;
        TerminalCell cell = coloredCell({255, 0, 0}, {0, 0, 255});
        TerminalCellSpan span{0, 1, &cell};
        TerminalUpdate update;
        update.spans = &span;
        update.spanCount = 1;
        update.colors = &colors;
        update.snappedSelection = Rect(0, 0);
        update.selectionColorMask = 3;
        update.selectionForeground = {1, 2, 3};
        update.selectionBackground = {4, 5, 6};

        const ReferenceImage image = renderer->render(update);

        STD_INSIST((cellPixel(image, 0, 0) == Color{1, 2, 3}));
        STD_INSIST((cellPixel(image, 1, 0) == Color{4, 5, 6}));
    }

    STD_TEST(SelectionOfWideContinuationHighlightsWholeGlyph) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        FakeFontpack fonts;
        fonts.bitmap[0] = 0;
        fonts.bitmap[1] = 0;
        fonts.bitmapLength = 2;
        configure(composer, fonts, 2, 1, 1, 1);
        ReferenceFixture renderer(composer);
        TerminalColors colors;
        TerminalCell cells[2]{
            coloredCell({255, 0, 0}, {0, 0, 255}),
            coloredCell({255, 0, 0}, {0, 0, 255}),
        };
        cells[0].dwidth = true;
        cells[1].dwidth_cont = true;
        TerminalCellSpan span{0, 2, cells};
        TerminalUpdate update;
        update.spans = &span;
        update.spanCount = 1;
        update.colors = &colors;
        update.snappedSelection = Rect(1, 0, 2, 0);
        update.snappedSelection.rectangular = true;
        update.selectionColorMask = 2;
        update.selectionBackground = {4, 5, 6};

        const ReferenceImage image = renderer->render(update);

        STD_INSIST((cellPixel(image, 0, 0) == Color{4, 5, 6}));
        STD_INSIST((cellPixel(image, 1, 0) == Color{4, 5, 6}));
    }

    STD_TEST(CompositesPremultipliedColorGlyph) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        FakeFontpack fonts;
        fonts.bitmap[0] = 100;
        fonts.bitmap[1] = 50;
        fonts.bitmap[2] = 0;
        fonts.bitmap[3] = 128;
        fonts.bitmapLength = 4;
        fonts.colorGlyph = true;
        configure(composer, fonts, 1, 1, 1, 1);
        ReferenceFixture renderer(composer);
        TerminalColors colors;
        TerminalCell cell{};
        cell.setBackground(CellColor::direct({0, 0, 100}));
        TerminalCellSpan span{0, 1, &cell};
        TerminalUpdate update;
        update.spans = &span;
        update.spanCount = 1;
        update.colors = &colors;

        const ReferenceImage image = renderer->render(update);

        STD_INSIST((cellPixel(image, 0, 0) == Color{100, 50, 49}));
    }

    STD_TEST(PassesStoredGraphemeAndStyleToFontpack) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        FakeFontpack fonts;
        fonts.bitmap[0] = 0;
        fonts.bitmapLength = 1;
        configure(composer, fonts, 1, 1, 1, 1);
        ReferenceFixture renderer(composer);
        TerminalColors colors;
        const u32 codepoints[] = {'a', 0x0301};
        TerminalCell stored{};
        stored.bold = true;
        stored.italic = true;
        composer.cellExtras->setGrapheme(stored, codepoints, 2);
        TerminalCellSpan span{0, 1, &stored};
        TerminalUpdate update;
        update.spans = &span;
        update.spanCount = 1;
        update.colors = &colors;

        renderer->render(update);

        STD_INSIST(fonts.receivedCount == 2);
        STD_INSIST(fonts.received[0] == codepoints[0]);
        STD_INSIST(fonts.received[1] == codepoints[1]);
        STD_INSIST(fonts.receivedStyle == FontStyle::BoldItalic);
        STD_INSIST(!fonts.receivedDoubleWidth);
    }
}
