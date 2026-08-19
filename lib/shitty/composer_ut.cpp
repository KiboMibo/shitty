/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "composer.h"

#include "cell_extra_store.h"
#include "grid_geometry.h"
#include "input_bindings.h"
#include "listener.h"
#include "mouse_frontend.h"
#include "options.h"

#include <std/mem/obj_pool.h>
#include <std/tst/ut.h>

using namespace stl;

namespace {
    struct StateListener final: public Listener {
        explicit StateListener(Composer& composer);

        void onListen(void* argument) override;

        Composer& composer;
        CellExtraStore* extras = nullptr;
        u16 columns = 0;
        u16 rows = 0;
        u16 pixelWidth = 0;
        u16 pixelHeight = 0;
        float contentScale = 0.0f;
        size_t calls = 0;
        bool argumentWasNull = false;
    };

    struct RemovingListener final: public Listener {
        void onListen(void* argument) override;

        size_t calls = 0;
    };
}

StateListener::StateListener(Composer& composer_)
    : composer(composer_)
{
}

void StateListener::onListen(void* argument) {
    extras = composer.cellExtras;
    columns = composer.columns;
    rows = composer.rows;
    pixelWidth = composer.pixelWidth;
    pixelHeight = composer.pixelHeight;
    contentScale = composer.contentScale;
    ++calls;
    argumentWasNull = argument == nullptr;
}

void RemovingListener::onListen(void*) {
    ++calls;
    unlink();
}

STD_TEST_SUITE(Composer) {
    STD_TEST(ConstructsCoreComponents) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());

        STD_INSIST(composer.pool == pool.mutPtr());
        STD_INSIST(composer.cellExtras != nullptr);
        STD_INSIST(composer.smallObjects != nullptr);
        STD_INSIST(composer.input != nullptr);
        STD_INSIST(composer.inputBindings != nullptr);
        STD_INSIST(composer.inputHandlers.front() != composer.inputHandlers.end());
        STD_INSIST(composer.fontResolvers.front() != composer.fontResolvers.end());
    }

    STD_TEST(PublishesContentScaleOnlyAfterChange) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        StateListener listener(composer);
        composer.contentScaleChangedListeners.pushBack(&listener);

        composer.setContentScale(1.5f);

        STD_INSIST(composer.contentScale == 1.5f);
        STD_INSIST(listener.contentScale == 1.5f);
        STD_INSIST(listener.calls == 1);
        STD_INSIST(listener.argumentWasNull);

        composer.setContentScale(1.5f);

        STD_INSIST(listener.calls == 1);
    }

    STD_TEST(DerivesPhysicalBorderFromSnapshotAndScale) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Options options;
        options.border = 7;
        composer.opts = &options;

        STD_INSIST(composer.borderPixels() == 7);

        composer.setContentScale(1.5f);

        STD_INSIST(composer.borderPixels() == 11);
    }

    STD_TEST(PublishesCommittedResizeState) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        StateListener listener(composer);
        composer.resizedListeners.pushBack(&listener);
        composer.setGlyphSize(8, 16);
        const Insets insets = composer.contentInsets();
        const u16 width = (u16)(gridPixelWidth(10, insets, composer.glyphWidth) + 3);
        const u16 height = (u16)(gridPixelHeight(4, insets, composer.glyphHeight) + 7);

        composer.resize(width, height);

        STD_INSIST(listener.calls == 1);
        STD_INSIST(listener.columns == 10);
        STD_INSIST(listener.rows == 4);
        STD_INSIST(listener.pixelWidth == width);
        STD_INSIST(listener.pixelHeight == height);

        composer.resize(width, height);

        STD_INSIST(listener.calls == 1);

        composer.resize(width + 1, height);

        STD_INSIST(listener.calls == 2);
        STD_INSIST(listener.columns == 10);
        STD_INSIST(listener.pixelWidth == width + 1);
    }

    STD_TEST(PublishesCellExtraReplacement) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        StateListener listener(composer);
        composer.cellExtrasChangedListeners.pushBack(&listener);
        auto* first = reinterpret_cast<CellExtraStore*>(uintptr_t(1));
        auto* second = reinterpret_cast<CellExtraStore*>(uintptr_t(2));

        composer.setCellExtras(first);

        STD_INSIST(listener.calls == 1);
        STD_INSIST(listener.extras == first);

        composer.setCellExtras(first);

        STD_INSIST(listener.calls == 1);

        composer.setCellExtras(second);

        STD_INSIST(listener.calls == 2);
        STD_INSIST(listener.extras == second);
    }

    STD_TEST(ListenerMayRemoveItselfDuringPublication) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        RemovingListener removing;
        StateListener trailing(composer);
        composer.contentScaleChangedListeners.pushBack(&removing);
        composer.contentScaleChangedListeners.pushBack(&trailing);

        composer.setContentScale(1.25f);
        composer.setContentScale(1.5f);

        STD_INSIST(removing.calls == 1);
        STD_INSIST(trailing.calls == 2);
    }

    // A1: contentInsets() is the only layout-facing source of geometry,
    // and what it reports on a side is the user's border plus whatever
    // chrome reserves there. The border half of that sum holds in every
    // build, before and after T5 and T6 add reserves, so it is asserted
    // as a lower bound rather than as an equality.
    STD_TEST(ContentInsetsCarryTheBorderOnEverySide) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Options options;
        options.border = 7;
        composer.opts = &options;

        const Insets atOneX = composer.contentInsets();

        STD_INSIST(composer.borderPixels() == 7);
        STD_INSIST(atOneX.left >= composer.borderPixels());
        STD_INSIST(atOneX.top >= composer.borderPixels());
        STD_INSIST(atOneX.right >= composer.borderPixels());
        STD_INSIST(atOneX.bottom >= composer.borderPixels());

        // The reserve is in backing pixels: a 1.5x display scales the
        // border with everything else, and the insets follow it.
        composer.setContentScale(1.5f);
        const Insets atOneAndAHalf = composer.contentInsets();

        STD_INSIST(composer.borderPixels() == 11);
        STD_INSIST(atOneAndAHalf.left >= composer.borderPixels());
        STD_INSIST(atOneAndAHalf.top >= composer.borderPixels());
        STD_INSIST(atOneAndAHalf.right >= composer.borderPixels());
        STD_INSIST(atOneAndAHalf.bottom >= composer.borderPixels());
        STD_INSIST(atOneAndAHalf.left > atOneX.left);
        STD_INSIST(atOneAndAHalf.top > atOneX.top);
    }

    // The other half of the same sum. This test was written by R3-test
    // as ChromeReservesAreStillZeroOnEverySide, a deliberate alarm: it
    // asserted that no reserve existed anywhere, so that the first task
    // to land one - T5, here - would be forced to come back and write
    // the asymmetric tests the plan asks for instead of quietly getting
    // a green run. T5 rewrote it into what the alarm was standing in
    // for: a window with nothing reserving anything still has four
    // sides that are exactly the border, and the sides that no chrome
    // claimed stay that way once one of them is claimed.
    STD_TEST(ChromeReservesAreZeroUntilSomethingClaimsASide) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Options options;
        options.border = 7;
        composer.opts = &options;

        const Insets insets = composer.contentInsets();

        STD_INSIST(composer.chromeReserve(ChromeSide::Top) == 0);
        STD_INSIST(composer.chromeReserve(ChromeSide::Right) == 0);
        STD_INSIST(composer.chromeReserve(ChromeSide::Bottom) == 0);
        STD_INSIST(composer.chromeReserve(ChromeSide::Left) == 0);
        STD_INSIST(insets.left == composer.borderPixels());
        STD_INSIST(insets.top == composer.borderPixels());
        STD_INSIST(insets.right == composer.borderPixels());
        STD_INSIST(insets.bottom == composer.borderPixels());

        options.border = 0;

        STD_INSIST(composer.contentInsets().left == 0);
        STD_INSIST(composer.contentInsets().top == 0);
        STD_INSIST(composer.contentInsets().right == 0);
        STD_INSIST(composer.contentInsets().bottom == 0);

        // Claiming one side leaves the other three exactly where they
        // were: two pieces of chrome own two different edges (T5 the
        // right, T6 the top) and never see each other's call.
        composer.setChromeReserve(ChromeSide::Right, 220);

        STD_INSIST(composer.chromeReserve(ChromeSide::Right) == 220);
        STD_INSIST(composer.contentInsets().right == 220);
        STD_INSIST(composer.contentInsets().top == 0);
        STD_INSIST(composer.contentInsets().bottom == 0);
        STD_INSIST(composer.contentInsets().left == 0);

        // And giving it back is the whole of turning the chrome off.
        composer.setChromeReserve(ChromeSide::Right, 0);

        STD_INSIST(composer.contentInsets().right == 0);
    }

    // A1, the first asymmetric inset in the tree: the sidebar's width is
    // an option in *logical points* and the insets are in backing
    // pixels, so the reserve has to be scaled exactly as the border is.
    // Getting this wrong is invisible at 1x and hands back half the
    // reserve on a Retina display, which puts the text under the panel
    // and misses the hit-test by the same 220 points.
    STD_TEST(ContentInsetsReserveTheSidebarOnTheRightInBackingPixels) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Options options;
        options.border = 7;
        composer.opts = &options;
        composer.setChromeReserve(ChromeSide::Right, 220);

        const Insets atOneX = composer.contentInsets();

        STD_INSIST(atOneX.right == 227);
        STD_INSIST(atOneX.top == 7);
        STD_INSIST(atOneX.bottom == 7);
        STD_INSIST(atOneX.left == 7);
        STD_INSIST(atOneX.right > atOneX.top);
        STD_INSIST(atOneX.right > atOneX.bottom);
        STD_INSIST(atOneX.right > atOneX.left);

        // Two backing pixels per point: the border doubles, and so must
        // the reserve sitting on top of it. A reserve stored in pixels
        // instead of points would still read 220 here.
        composer.setContentScale(2.0f);
        const Insets atTwoX = composer.contentInsets();

        STD_INSIST(atTwoX.top == 14);
        STD_INSIST(atTwoX.right == 454);
        STD_INSIST(atTwoX.right - atTwoX.top == 440);
        STD_INSIST(atTwoX.left == 14);
        STD_INSIST(atTwoX.bottom == 14);
    }

    // The sidebar takes its width out of the grid, not out of the
    // window: the surface is the same and the terminal is narrower by
    // exactly the reserve, which is what cmd+b is for (A7) and what the
    // shell hears about through the resize this publishes.
    STD_TEST(ResizeGivesTheSidebarItsColumnsBackWhenItIsHidden) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        StateListener listener(composer);
        composer.resizedListeners.pushBack(&listener);
        Options options;
        options.border = 0;
        composer.opts = &options;
        composer.setContentScale(2.0f);
        composer.setGlyphSize(8, 16);
        // A whole number of cells wide, so "fewer by the reserve" is an
        // exact statement rather than one rounded into invisibility.
        composer.resize(1600, 800);
        const u16 wide = composer.columns;

        STD_INSIST(wide == 200);

        composer.setChromeReserve(ChromeSide::Right, 220);

        // 220 points at 2x is 440 backing pixels is 55 columns of 8.
        STD_INSIST(composer.columns == wide - 55);
        STD_INSIST(composer.pixelWidth == 1600);
        // The rows are the sidebar's business on no axis at all.
        STD_INSIST(composer.rows == 50);
        // And the shell heard about it: setChromeReserve() publishes the
        // resize itself, so cmd+b needs no second mechanism to make the
        // pty follow.
        STD_INSIST(listener.calls == 2);
        STD_INSIST(listener.columns == wide - 55);

        composer.setChromeReserve(ChromeSide::Right, 0);

        STD_INSIST(composer.columns == wide);
        STD_INSIST(listener.calls == 3);
    }

    // The pointer's half of the same asymmetry (A1). It lives here
    // rather than in mouse_frontend_ut.cpp because what is under test is
    // a Composer whose four sides differ - mouseGeometry() is only the
    // shortest way to ask it where the content box ends.
    STD_TEST(PointerStopsAtTheSidebarsEdgeNotTheWindows) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Options options;
        options.border = 0;
        composer.opts = &options;
        composer.setGlyphSize(8, 16);
        composer.setChromeReserve(ChromeSide::Right, 20);
        // 196 wide less the 20 the panel holds is 176: exactly 22 cells,
        // so the last column ends where the panel begins.
        composer.resize(196, 160);

        STD_INSIST(composer.columns == 22);

        const MouseGeometry geometry = mouseGeometry(composer);
        u16 column = 0;
        u16 row = 0;

        // The last pixel of the last column is still the terminal's.
        STD_INSIST(mouseCell(175, 0, geometry, column, row));
        STD_INSIST(column == 21);
        STD_INSIST(row == 0);

        // The first pixel of the panel is not, and neither is anything
        // right of it: a click on a tab must never also land in the
        // grid behind it.
        STD_INSIST(!mouseCell(176, 0, geometry, column, row));
        STD_INSIST(!mouseCell(195, 0, geometry, column, row));
        STD_INSIST(column == 21);

        // The left edge is untouched by a reserve on the right - the
        // side the panel is on is not a detail the mapping may guess.
        STD_INSIST(mouseCell(0, 0, geometry, column, row));
        STD_INSIST(column == 0);
    }

    // resize() and contentInsets() are the two directions of one
    // formula: the grid a surface reports has to be the grid that
    // surface's content box holds, at every size including the ones
    // where the box ends mid-cell or does not exist at all.
    STD_TEST(ResizeCountsTheGridOutOfTheContentInsets) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Options options;
        options.border = 7;
        composer.opts = &options;
        composer.setContentScale(1.5f);
        composer.setGlyphSize(8, 16);

        const Insets insets = composer.contentInsets();

        for (u16 pixels = 1; pixels < 600; pixels = (u16)(pixels + 7)) {
            composer.resize(pixels, pixels);

            STD_INSIST(composer.columns == gridColumns(pixels, insets, composer.glyphWidth));
            STD_INSIST(composer.rows == gridRows(pixels, insets, composer.glyphHeight));
        }

        // A surface smaller than its own reserve still reports a usable
        // grid rather than a zero-column terminal.
        composer.resize(1, 1);

        STD_INSIST(composer.columns == 1);
        STD_INSIST(composer.rows == 1);

        // And a surface sized for N cells reports exactly N back.
        const u16 width = (u16)(gridPixelWidth(37, insets, composer.glyphWidth));
        const u16 height = (u16)(gridPixelHeight(11, insets, composer.glyphHeight));
        composer.resize(width, height);

        STD_INSIST(composer.columns == 37);
        STD_INSIST(composer.rows == 11);
    }
}
