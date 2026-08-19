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
#include "grid_geometry.h"
#include "options.h"
#include "screen.h"
#include "vterm.h"

#include <plt/platform_headless.h>

#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/tst/ut.h>

using namespace stl;

namespace {
    struct FakeFontpack final: public Fontpack {
        u16 getPx() const override;
        u16 getPy() const override;
        float boxDrawingStroke() const override;
        bool hasBold() const override;
        bool hasItalic() const override;
        bool hasBoldItalic() const override;
        Font* resolveFace(const u32* codepoints, size_t count) override;
        void adoptFaceFor(const FontFaceMiss& miss) override;
        Font* styledFace(Font* face, FontStyle style) const override;
    };

    static void configure(Composer& composer, FakeFontpack& fonts, u16 columns, u16 rows, u16 glyphWidth, u16 glyphHeight) {
        composer.fonts = &fonts;
        composer.setGlyphSize(glyphWidth, glyphHeight);
        composer.setCellExtras(CellExtraStore::create(composer, (size_t)(columns)*rows));
        const Insets insets = composer.contentInsets();
        composer.resize((u16)(gridPixelWidth(columns, insets, glyphWidth)), (u16)(gridPixelHeight(rows, insets, glyphHeight)));
    }

    static Color pixel(const ReferenceImage& image, u16 x, u16 y) {
        const size_t index = 3 * ((size_t)(y)*image.width + x);
        return {
            image.pixels[index],
            image.pixels[index + 1],
            image.pixels[index + 2],
        };
    }

    static Color cellPixel(const ReferenceImage& image, u16 x, u16 y) {
        return pixel(image, x, y);
    }

    static TerminalCell coloredCell(Color foreground, Color background) {
        TerminalCell cell{};
        cell.setForeground(CellColor::direct(foreground));
        cell.setBackground(CellColor::direct(background));
        return cell;
    }

    struct ReferenceFixture {
        explicit ReferenceFixture(Composer& composer) {
            const size_t bytes = (size_t)(composer.pixelWidth) * composer.pixelHeight * 3;
            while (pixels.length() < bytes) {
                pixels.pushBack(0);
            }
            target.pixels = pixels.mutData();
            target.length = pixels.length();
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

        Vector<u8> pixels;
        plt::HeadlessRenderTarget target;
        stl::ObjPool::Ref rendererPool = stl::ObjPool::fromMemory();
        ReferenceRenderer* renderer;
    };

    // A2: what a split hands the renderer - a second terminal on the
    // same composer, with its own screen, its own colours and its own
    // strip arena. ScreenFixture below is one of these plus the composer
    // they share.
    static void writeTextTo(Screen& screen, u16 row, u16 column, const char* text, const TerminalCell& attrs) {
        for (size_t index = 0; text[index] != 0; ++index) {
            const u32 codepoint = (u32)(u8)(text[index]);
            screen.writeGrapheme(row, (u16)(column + index), &codepoint, 1, false, attrs, 0, 0, attrs);
        }
    }

    static TerminalUpdate captureFrom(Composer& composer, Screen& screen, const TerminalColors& colors, Vector<TerminalRow>& rows) {
        screen.expose();
        rows.grow((size_t)(composer.rows));
        const ScreenFrame frame = screen.captureFrame(rows.mutData());
        TerminalUpdate update;
        update.rows = rows.data();
        update.rowCount = frame.damagedRows;
        update.colors = &colors;
        update.shapes = &screen;
        return update;
    }

    struct ScreenFixture {
        // A1: `border` is the user option, which contentInsets() turns
        // into the reserve on all four sides; zero keeps the grid in the
        // surface corner, as every other test here expects.
        // `topReserve` is a chrome reserve in logical points on the top
        // edge alone - what T6's auto-hiding title bar strip claims -
        // and the only way to build a Composer here whose insets are not
        // the same number on all four sides.
        explicit ScreenFixture(u16 columns, u16 rows, u16 border = 0, u16 topReserve = 0);

        void writeText(u16 row, u16 column, const char* text, const TerminalCell& attrs);
        TerminalUpdate capture();

        // Declared before `pool` on purpose: members die in reverse
        // declaration order, and the pool's Composer keeps `&options`
        // while its Screen keeps `&colors`. Put the pool first and both
        // referents are gone before the objects that point at them.
        Options options;
        TerminalColors colors;
        ObjPool::Ref pool = ObjPool::fromMemory();
        Composer* composer = nullptr;
        Screen* screen = nullptr;
        Vector<TerminalRow> rows;
    };

    static bool cellHasInk(const ReferenceImage& image, u16 glyphWidth, u16 glyphHeight, u16 cell, Color background) {
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

ScreenFixture::ScreenFixture(u16 columns, u16 rows, u16 border, u16 topReserve) {
    colors.defaultForeground = {1, 2, 3};
    colors.defaultBackground = {4, 5, 6};
    composer = pool->make<Composer>(pool.mutPtr());
    options.border = border;
    composer->opts = &options;
    // Before the glyph size and the resize below: a reserve claimed
    // before there is a grid is simply remembered, and the first resize
    // counts the grid out of it.
    composer->setChromeReserve(ChromeSide::Top, topReserve);
    // Only the embedded resolver: the tests must not depend on system
    // fonts.
    while (!composer->fontResolvers.empty()) {
        composer->fontResolvers.popFront();
    }
    composer->fontResolvers.pushBack(createEmbeddedFontResolver(*composer));
    composer->fonts = Fontpack::create(*composer, *pool, nullptr, 0, 16);
    composer->setGlyphSize(composer->fonts->getPx(), composer->fonts->getPy());
    composer->setCellExtras(CellExtraStore::create(*composer, (size_t)(columns)*rows));
    const Insets insets = composer->contentInsets();
    composer->resize((u16)(gridPixelWidth(columns, insets, composer->glyphWidth)), (u16)(gridPixelHeight(rows, insets, composer->glyphHeight)));
    screen = Screen::createPrimary(*composer, *pool, columns, rows, &colors, 8);
}

void ScreenFixture::writeText(u16 row, u16 column, const char* text, const TerminalCell& attrs) {
    writeTextTo(*screen, row, column, text, attrs);
}

TerminalUpdate ScreenFixture::capture() {
    return captureFrom(*composer, *screen, colors, rows);
}

u16 FakeFontpack::getPx() const {
    return 0;
}

u16 FakeFontpack::getPy() const {
    return 0;
}

float FakeFontpack::boxDrawingStroke() const {
    return 0.0f;
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

Font* FakeFontpack::styledFace(Font* face, FontStyle) const {
    return face;
}

Font* FakeFontpack::resolveFace(const u32*, size_t) {
    return nullptr;
}

void FakeFontpack::adoptFaceFor(const FontFaceMiss&) {
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

    // A1: cell 0,0 starts at the content insets, not at the surface
    // corner. Production still asks for a uniform border, so what this
    // pins is the origin being applied, once, on both axes - dropping it,
    // halving it or doubling it moves the cell off these pixels.
    STD_TEST(PlacesTheGridAtTheContentInsets) {
        constexpr u16 border = 6;
        ScreenFixture fx(2, 1, border);
        TerminalCell attrs{};
        attrs.setForeground(CellColor::direct({255, 0, 0}));
        attrs.setBackground(CellColor::direct({0, 0, 255}));
        // A space carries the cell's background and no ink, so the whole
        // cell is one color to look for.
        fx.writeText(0, 0, " ", attrs);
        ReferenceFixture renderer(*fx.composer);

        const ReferenceImage image = renderer->render(fx.capture());

        STD_INSIST(image.pixels != nullptr);
        const Insets insets = fx.composer->contentInsets();
        STD_INSIST(insets.left == border && insets.top == border);
        STD_INSIST((cellPixel(image, insets.left, insets.top) == Color{0, 0, 255}));
        STD_INSIST((cellPixel(image, (u16)(insets.left + fx.composer->glyphWidth - 1), (u16)(insets.top + fx.composer->glyphHeight - 1)) == Color{0, 0, 255}));
        // The pixel one step back on either axis is outside every cell.
        STD_INSIST((!(cellPixel(image, (u16)(insets.left - 1), insets.top) == Color{0, 0, 255})));
        STD_INSIST((!(cellPixel(image, insets.left, (u16)(insets.top - 1)) == Color{0, 0, 255})));
    }

    // R4-test, debt of wave 3, item 2 - the one the whole wave was
    // sequenced around. The reference renderer reads contentInsets()
    // itself and pairs `left` with x and `top` with y; until T6 put a
    // reserve on the top edge those two were the same number, so
    // transposing them (mutant M14 of T4, reproduced as R7 by R3-test)
    // survived every test in the tree and the entire `build test -k`
    // graph. With the strip reserved they name different pixels, and the
    // transposed origin puts cell 0,0 where nothing may draw.
    STD_TEST(PlacesTheGridAtInsetsThatDifferOnEveryAxis) {
        constexpr u16 border = 6;
        constexpr u16 strip = 32;
        ScreenFixture fx(8, 3, border, strip);
        TerminalCell attrs{};
        attrs.setForeground(CellColor::direct({255, 0, 0}));
        attrs.setBackground(CellColor::direct({0, 0, 255}));
        // A space carries the cell's background and no ink, so the whole
        // cell is one color to look for.
        fx.writeText(0, 0, " ", attrs);
        ReferenceFixture renderer(*fx.composer);

        const ReferenceImage image = renderer->render(fx.capture());

        STD_INSIST(image.pixels != nullptr);
        const Insets insets = fx.composer->contentInsets();
        STD_INSIST(insets.left == border);
        STD_INSIST(insets.bottom == border);
        STD_INSIST(insets.right == border);
        STD_INSIST(insets.top == border + strip);
        // The premise of the whole test: the two coordinates of the
        // origin are different numbers now.
        STD_INSIST(insets.top != insets.left);

        // Cell 0,0 begins at (left, top) and fills its glyph box.
        STD_INSIST((cellPixel(image, insets.left, insets.top) == Color{0, 0, 255}));
        STD_INSIST((cellPixel(image, (u16)(insets.left + fx.composer->glyphWidth - 1), (u16)(insets.top + fx.composer->glyphHeight - 1)) == Color{0, 0, 255}));
        // The pixel one step back on either axis is outside every cell,
        // and the two axes are checked separately: this is what tells a
        // dropped `top` from a dropped `left`.
        STD_INSIST((!(cellPixel(image, (u16)(insets.left - 1), insets.top) == Color{0, 0, 255})));
        STD_INSIST((!(cellPixel(image, insets.left, (u16)(insets.top - 1)) == Color{0, 0, 255})));

        // And the transposed origin - x taken from `top`, y from `left`,
        // which is M14 exactly - names a pixel inside this surface that
        // belongs to no cell at all.
        STD_INSIST(insets.top < image.width);
        STD_INSIST(insets.left < image.height);
        STD_INSIST((!(cellPixel(image, insets.top, insets.left) == Color{0, 0, 255})));
        STD_INSIST((!(cellPixel(image, (u16)(insets.top + fx.composer->glyphWidth - 1), (u16)(insets.left + fx.composer->glyphHeight - 1)) == Color{0, 0, 255})));
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
        TerminalRow row{&cell, 0, 0};
        TerminalUpdate update;
        update.rows = &row;
        update.rowCount = 1;
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
        TerminalRow row{initial, 0, 0};
        TerminalUpdate update;
        update.rows = &row;
        update.rowCount = 1;
        update.colors = &colors;

        ReferenceImage image = renderer->render(update);
        STD_INSIST((cellPixel(image, 0, 0) == Color{10, 20, 30}));
        STD_INSIST((cellPixel(image, 1, 0) == Color{40, 50, 60}));

        // A frame with no damaged rows leaves the retained cells alone.
        initial[1].setBackground(CellColor::direct({70, 80, 90}));
        image = renderer->render(update);
        STD_INSIST((cellPixel(image, 1, 0) == Color{70, 80, 90}));
        update.rowCount = 0;
        initial[1].setBackground(CellColor::direct({40, 50, 60}));
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
        TerminalRow row{&cell, 0, 0};
        TerminalUpdate update;
        update.rows = &row;
        update.rowCount = 1;
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
        TerminalRow row{cells, 0, 0};
        TerminalUpdate update;
        update.rows = &row;
        update.rowCount = 1;
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
        update.overlayCells = preedit;
        update.overlayRow = 0;
        update.overlayColumn = 0;
        update.overlayCount = 2;

        image = renderer->render(update);
        STD_INSIST(image.pixels != nullptr);
        STD_INSIST(!cellHasInk(image, width, height, 0, {4, 5, 6}));
        STD_INSIST(!cellHasInk(image, width, height, 1, {4, 5, 6}));
    }

    // A2. Two panes, one frame, two rectangles - and each pane owning
    // its rectangle is what all four assertions below are about.
    //
    // The surface holds a 4x3 grid with a border on every side; the two
    // panes take a row each from the top, which leaves the bottom row of
    // the surface belonging to no pane at all. Each pane draws the whole
    // three-row grid of its own terminal into its one-row rectangle.
    // What the panes must not share:
    //
    //   - their clear. A pane's padding carries its own terminal's
    //     background, so a clear that took the whole target would paint
    //     the first pane over with the second one's colour.
    //   - their origin. The lower pane's cell 0,0 lands at its own
    //     rectangle plus the insets, not at the surface corner.
    //   - their pixels. The rows that do not fit a pane would land on
    //     the pane below it and on the strip below them both; the clip
    //     to the pane rectangle is what stops them.
    STD_TEST(DrawsTwoPanesInTheirOwnRectangles) {
        constexpr u16 border = 3;
        ScreenFixture fx(4, 3, border);
        auto* const other = fx.pool->make<TerminalColors>();
        // The pool outlives the screen that keeps a pointer to these.
        other->defaultForeground = {1, 2, 3};
        other->defaultBackground = {0, 255, 0};
        Screen* const second = Screen::createPrimary(*fx.composer, *fx.pool, 4, 3, other, 8);
        Vector<TerminalRow> secondRows;

        TerminalCell first{};
        first.setForeground(CellColor::direct({255, 0, 0}));
        first.setBackground(CellColor::direct({255, 0, 0}));
        TerminalCell spilled{};
        spilled.setForeground(CellColor::direct({255, 255, 0}));
        spilled.setBackground(CellColor::direct({255, 255, 0}));
        TerminalCell lower{};
        lower.setForeground(CellColor::direct({255, 255, 255}));
        lower.setBackground(CellColor::direct({255, 255, 255}));
        // A space carries the cell's background and no ink, so each cell
        // is one colour to look for.
        fx.writeText(0, 0, " ", first);
        writeTextTo(*second, 0, 0, " ", lower);
        // The row that does not fit either pane, in the pane that draws
        // last: nothing may overwrite it afterwards, so where it lands is
        // where it stays.
        writeTextTo(*second, 1, 0, " ", spilled);

        ReferenceFixture renderer(*fx.composer);
        const u16 paneHeight = (u16)(border + fx.composer->glyphHeight);
        STD_INSIST(fx.composer->pixelHeight > 2 * paneHeight);
        const PaneUpdate panes[2] = {
            {PixelRect{0, 0, fx.composer->pixelWidth, paneHeight}, fx.capture()},
            {PixelRect{0, paneHeight, fx.composer->pixelWidth, paneHeight}, captureFrom(*fx.composer, *second, *other, secondRows)},
        };

        STD_INSIST(renderer.renderer->update(panes, 2));
        const ReferenceImage image = renderer.renderer->image();
        STD_INSIST(image.pixels != nullptr);

        // Each pane cleared its own rectangle with its own background.
        STD_INSIST((cellPixel(image, 0, 0) == Color{4, 5, 6}));
        STD_INSIST((cellPixel(image, 0, paneHeight) == Color{0, 255, 0}));
        // Each pane put cell 0,0 at its own rectangle plus the insets.
        STD_INSIST((cellPixel(image, border, border) == Color{255, 0, 0}));
        STD_INSIST((cellPixel(image, border, (u16)(paneHeight + border)) == Color{255, 255, 255}));
        // The row that did not fit its pane is nowhere on the surface,
        // and the strip no pane claimed was never written at all - the
        // fixture hands over a zeroed target.
        for (u16 y = 0; y < image.height; ++y) {
            for (u16 x = 0; x < image.width; ++x) {
                STD_INSIST((!(cellPixel(image, x, y) == Color{255, 255, 0})));
                if (y >= 2 * paneHeight) {
                    STD_INSIST((cellPixel(image, x, y) == Color{0, 0, 0}));
                }
            }
        }
    }

    // The same frame with the panes handed over in the other order. A
    // pane is placed by its rectangle and not by its turn, so the image
    // must come out the same - which is what tells a renderer that reads
    // the list from a renderer that draws whatever it was given last
    // wherever the previous pane was.
    STD_TEST(PaneOrderDoesNotMovePanes) {
        constexpr u16 border = 3;
        ScreenFixture fx(4, 2, border);
        auto* const other = fx.pool->make<TerminalColors>();
        other->defaultForeground = {1, 2, 3};
        other->defaultBackground = {0, 255, 0};
        Screen* const second = Screen::createPrimary(*fx.composer, *fx.pool, 4, 2, other, 8);
        Vector<TerminalRow> secondRows;

        TerminalCell top{};
        top.setForeground(CellColor::direct({255, 0, 0}));
        top.setBackground(CellColor::direct({255, 0, 0}));
        TerminalCell bottom{};
        bottom.setForeground(CellColor::direct({255, 255, 255}));
        bottom.setBackground(CellColor::direct({255, 255, 255}));
        fx.writeText(0, 0, " ", top);
        writeTextTo(*second, 0, 0, " ", bottom);

        ReferenceFixture renderer(*fx.composer);
        const u16 half = (u16)(fx.composer->pixelHeight / 2);
        const PaneUpdate panes[2] = {
            {PixelRect{0, half, fx.composer->pixelWidth, half}, captureFrom(*fx.composer, *second, *other, secondRows)},
            {PixelRect{0, 0, fx.composer->pixelWidth, half}, fx.capture()},
        };

        STD_INSIST(renderer.renderer->update(panes, 2));
        const ReferenceImage image = renderer.renderer->image();
        STD_INSIST(image.pixels != nullptr);

        STD_INSIST((cellPixel(image, 0, 0) == Color{4, 5, 6}));
        STD_INSIST((cellPixel(image, 0, half) == Color{0, 255, 0}));
        STD_INSIST((cellPixel(image, border, border) == Color{255, 0, 0}));
        STD_INSIST((cellPixel(image, border, (u16)(half + border)) == Color{255, 255, 255}));
    }

    // The pane-less update() is the one-pane frame spelled shorter: the
    // pane it builds is the whole surface, so the padding it clears and
    // the origin it draws at are the surface's own.
    STD_TEST(ThePaneLessUpdateFillsTheSurface) {
        constexpr u16 border = 3;
        ScreenFixture fx(4, 2, border);
        TerminalCell attrs{};
        attrs.setForeground(CellColor::direct({255, 0, 0}));
        attrs.setBackground(CellColor::direct({0, 0, 255}));
        fx.writeText(1, 3, " ", attrs);
        ReferenceFixture renderer(*fx.composer);

        const ReferenceImage image = renderer->render(fx.capture());

        STD_INSIST(image.pixels != nullptr);
        // The last cell of the last row, which only a pane the size of
        // the surface reaches.
        const u16 x = (u16)(border + 3 * fx.composer->glyphWidth);
        const u16 y = (u16)(border + fx.composer->glyphHeight);
        STD_INSIST((cellPixel(image, x, y) == Color{0, 0, 255}));
        STD_INSIST((cellPixel(image, (u16)(image.width - 1), (u16)(image.height - 1)) == Color{4, 5, 6}));
    }
}
