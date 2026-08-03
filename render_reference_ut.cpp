/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "render_reference.h"

#include "cell_extra_store.h"
#include "composer.h"
#include "font_embedded.h"
#include "font_pack.h"
#include "font_resolver.h"
#include "options.h"
#include "screen.h"
#include "vterm.h"

#include <plt/platform_headless.h>

#include <std/lib/vector.h>
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
        FontGlyph glyph(const u32* codepoints, size_t count, FontStyle style, bool doubleWidth) override;
        Font* resolveFace(const u32* codepoints, size_t count) override;
        Font* styledFace(Font* face, FontStyle style) const override;
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
            : pixels((size_t)(composer.pixelWidth) * composer.pixelHeight * 3)
        {
            target.pixels = pixels.data();
            target.length = pixels.size();
            target.width = composer.pixelWidth;
            target.height = composer.pixelHeight;
            target.stride = composer.pixelWidth * 3;
            renderer = ReferenceRenderer::create(
                composer,
                *rendererPool,
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
        stl::ObjPool::Ref rendererPool = stl::ObjPool::fromMemory();
        ReferenceRenderer* renderer;
    };

    struct ScreenFixture {
        ScreenFixture(u16 columns, u16 rows);

        void writeText(u16 row, u16 column, const char* text, const TerminalCell& attrs);
        TerminalUpdate capture();

        ObjPool::Ref pool = ObjPool::fromMemory();
        TerminalColors colors;
        Composer* composer = nullptr;
        Screen* screen = nullptr;
        Vector<TerminalCellSpan> spans;
    };

    bool cellHasInk(const ReferenceImage& image, u16 glyphWidth, u16 glyphHeight, u16 cell, Color background) {
        for (u16 y = 0; y < glyphHeight; ++y) {
            for (u16 x = 0; x < glyphWidth; ++x) {
                if (!(cellPixel(image, (u16)(cell * glyphWidth + x), y) == background)) {
                    return true;
                }
            }
        }
        return false;
    }
}

ScreenFixture::ScreenFixture(u16 columns, u16 rows) {
    colors.defaultForeground = {1, 2, 3};
    colors.defaultBackground = {4, 5, 6};
    composer = pool->make<Composer>(pool.mutPtr());
    // Only the embedded resolver: the tests must not depend on system
    // fonts.
    while (!composer->fontResolvers.empty()) {
        composer->fontResolvers.popFront();
    }
    composer->fontResolvers.pushBack(createEmbeddedFontResolver(*composer));
    composer->fonts = Fontpack::create(*composer, *pool, nullptr, 0, 16);
    composer->setGlyphSize(composer->fonts->getPx(), composer->fonts->getPy());
    composer->setCellExtras(CellExtraStore::create(*composer, (size_t)(columns)*rows));
    composer->resize((u16)(columns * composer->glyphWidth + 2 * opts.border), (u16)(rows * composer->glyphHeight + 2 * opts.border));
    screen = Screen::createPrimary(*composer, *pool, columns, rows, &colors, 8);
}

void ScreenFixture::writeText(u16 row, u16 column, const char* text, const TerminalCell& attrs) {
    for (size_t index = 0; text[index] != 0; ++index) {
        const u32 codepoint = (u32)(u8)(text[index]);
        screen->writeGrapheme(row, (u16)(column + index), &codepoint, 1, false, attrs, 0, 0, attrs);
    }
}

TerminalUpdate ScreenFixture::capture() {
    screen->expose();
    spans.grow((size_t)(composer->rows) * ((composer->columns + 1u) / 2u) + 2);
    const ScreenFrame frame = screen->captureFrame(spans.mutData());
    TerminalUpdate update;
    update.spans = spans.data();
    update.spanCount = frame.damage.spanCount;
    update.colors = &colors;
    update.shapes = screen;
    return update;
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

FontGlyph FakeFontpack::glyph(const u32*, size_t, FontStyle, bool) {
    return {};
}

Font* FakeFontpack::styledFace(Font* face, FontStyle) const {
    return face;
}

Font* FakeFontpack::resolveFace(const u32*, size_t) {
    return nullptr;
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

    STD_TEST(ScreenStripsBlendInkOverBackground) {
        ScreenFixture fx(4, 1);
        TerminalCell attrs{};
        attrs.setForeground(CellColor::direct({255, 0, 0}));
        attrs.setBackground(CellColor::direct({0, 0, 255}));
        fx.writeText(0, 0, "a", attrs);
        ReferenceFixture renderer(*fx.composer);

        const ReferenceImage image = renderer->render(fx.capture());

        STD_INSIST(image.pixels != nullptr);
        const u16 width = fx.composer->glyphWidth;
        const u16 height = fx.composer->glyphHeight;
        // The glyph cell blends ink toward the foreground; a solid-core
        // pixel is nearly pure red.
        bool solid = false;
        bool background = false;
        for (u16 y = 0; y < height; ++y) {
            for (u16 x = 0; x < width; ++x) {
                const Color pixel = cellPixel(image, x, y);
                solid = solid || (pixel.red > 200 && pixel.blue < 60);
                background = background || pixel == Color{0, 0, 255};
            }
        }
        STD_INSIST(solid);
        STD_INSIST(background);
        // The blank neighbour renders the default background exactly.
        for (u16 y = 0; y < height; ++y) {
            for (u16 x = 0; x < width; ++x) {
                STD_INSIST((cellPixel(image, (u16)(width + x), y) == Color{4, 5, 6}));
            }
        }
    }

    STD_TEST(InverseAndScreenReverseCancelEachOther) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        FakeFontpack fonts;
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

        // No strips reach this renderer, so the cell paints its
        // background: the inverted foreground, then the original
        // background when screen reverse cancels the inversion.
        ReferenceImage image = renderer->render(update);
        STD_INSIST((cellPixel(image, 0, 0) == Color{255, 0, 0}));

        update.screenReverse = true;
        image = renderer->render(update);
        STD_INSIST((cellPixel(image, 0, 0) == Color{0, 0, 255}));
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

        STD_INSIST((cellPixel(image, 0, 0) == Color{4, 5, 6}));
        STD_INSIST((cellPixel(image, 1, 0) == Color{4, 5, 6}));
    }

    STD_TEST(SelectionOfWideContinuationHighlightsWholeGlyph) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        FakeFontpack fonts;
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

    STD_TEST(GraphemeClusterRendersInkFromStrips) {
        ScreenFixture fx(2, 1);
        TerminalCell attrs{};
        attrs.setForeground(CellColor::defaultForeground());
        attrs.setBackground(CellColor::defaultBackground());
        const u32 codepoints[] = {'a', 0x0301};
        fx.screen->writeGrapheme(0, 0, codepoints, 2, false, attrs, 0, 0, attrs);
        ReferenceFixture renderer(*fx.composer);

        const ReferenceImage image = renderer->render(fx.capture());

        STD_INSIST(image.pixels != nullptr);
        STD_INSIST(cellHasInk(image, fx.composer->glyphWidth, fx.composer->glyphHeight, 0, {4, 5, 6}));
    }

    STD_TEST(ColorStripCompositesOverBackground) {
        ScreenFixture fx(4, 1);
        TerminalCell attrs{};
        attrs.setForeground(CellColor::defaultForeground());
        attrs.setBackground(CellColor::defaultBackground());
        const u32 emoji = 0x1f600;
        fx.screen->writeGrapheme(0, 0, &emoji, 1, true, attrs, 0, 0, attrs);
        ReferenceFixture renderer(*fx.composer);

        const ReferenceImage image = renderer->render(fx.capture());

        STD_INSIST(image.pixels != nullptr);
        const u16 width = fx.composer->glyphWidth;
        const u16 height = fx.composer->glyphHeight;
        size_t chromatic = 0;
        for (u16 y = 0; y < height; ++y) {
            for (u16 x = 0; x < 2 * width; ++x) {
                const Color pixel = cellPixel(image, x, y);
                const u8 high = pixel.red > pixel.green ? pixel.red : pixel.green;
                const u8 low = pixel.red < pixel.green ? pixel.red : pixel.green;
                chromatic += (high > pixel.blue ? high : pixel.blue) != (low < pixel.blue ? low : pixel.blue);
            }
        }
        STD_INSIST(chromatic > 16);
    }

    STD_TEST(PreeditOverlayCoversUnderlyingStrips) {
        ScreenFixture fx(4, 1);
        TerminalCell attrs{};
        attrs.setForeground(CellColor::defaultForeground());
        attrs.setBackground(CellColor::defaultBackground());
        fx.writeText(0, 0, "ab", attrs);
        ReferenceFixture renderer(*fx.composer);

        ReferenceImage image = renderer->render(fx.capture());
        STD_INSIST(image.pixels != nullptr);
        const u16 width = fx.composer->glyphWidth;
        const u16 height = fx.composer->glyphHeight;
        STD_INSIST(cellHasInk(image, width, height, 0, {4, 5, 6}));

        // A blank preedit window over the text hides its strips: the
        // covered cells fall back to their plain background.
        TerminalUpdate update = fx.capture();
        TerminalCell preedit[2]{attrs, attrs};
        TerminalCellSpan overlay{0, 2, preedit};
        Vector<TerminalCellSpan> spans;
        spans.grow(update.spanCount + 1);
        for (size_t index = 0; index < update.spanCount; ++index) {
            spans.pushBack(update.spans[index]);
        }
        spans.pushBack(overlay);
        update.spans = spans.data();
        update.overlaySpan = update.spanCount;
        update.spanCount = update.spanCount + 1;

        image = renderer->render(update);
        STD_INSIST(image.pixels != nullptr);
        STD_INSIST(!cellHasInk(image, width, height, 0, {4, 5, 6}));
        STD_INSIST(!cellHasInk(image, width, height, 1, {4, 5, 6}));
    }
}
