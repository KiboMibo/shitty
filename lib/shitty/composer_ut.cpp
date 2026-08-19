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

    // The other half of the same sum, and the reason R3-test could not
    // write the asymmetric tests the plan asks for: no chrome reserves
    // exist yet, so every side is exactly the border and no test that
    // goes through a Composer can tell an Insets from the scalar it
    // replaced. T5 (sidebar, right) and T6 (hover strip, top) land the
    // first reserves; when they do, this test fails, and that failure is
    // the signal to write the tests it stands in for - the renderer's
    // grid origin and the pointer mapping, both driven off a Composer
    // whose four sides differ.
    STD_TEST(ChromeReservesAreStillZeroOnEverySide) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Options options;
        options.border = 7;
        composer.opts = &options;

        const Insets insets = composer.contentInsets();

        STD_INSIST(insets.left == composer.borderPixels());
        STD_INSIST(insets.top == composer.borderPixels());
        STD_INSIST(insets.right == composer.borderPixels());
        STD_INSIST(insets.bottom == composer.borderPixels());

        options.border = 0;

        STD_INSIST(composer.contentInsets().left == 0);
        STD_INSIST(composer.contentInsets().top == 0);
        STD_INSIST(composer.contentInsets().right == 0);
        STD_INSIST(composer.contentInsets().bottom == 0);
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
