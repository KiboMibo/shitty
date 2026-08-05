/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "composer.h"

#include "cell_extra_store.h"
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

    STD_TEST(PublishesCommittedResizeState) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        StateListener listener(composer);
        composer.resizedListeners.pushBack(&listener);
        composer.setGlyphSize(8, 16);
        const u16 width = 2 * composer.opts->border + 10 * composer.glyphWidth + 3;
        const u16 height = 2 * composer.opts->border + 4 * composer.glyphHeight + 7;

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

    // The tab bar is a band above the grid: it takes height and nothing
    // else. The grid loses exactly the rows the band covers, the columns
    // are untouched, and the framebuffer is unchanged - the band lives
    // inside it rather than beside it.
    STD_TEST(TopInsetTakesRowsFromTheGridAndNothingElse) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        composer.setGlyphSize(8, 16);
        const u16 width = 2 * composer.opts->border + 10 * composer.glyphWidth;
        const u16 height = 2 * composer.opts->border + 4 * composer.glyphHeight;
        composer.resize(width, height);
        STD_INSIST(composer.rows == 4);
        STD_INSIST(composer.columns == 10);

        composer.setTopInset(composer.glyphHeight);

        STD_INSIST(composer.rows == 3);
        STD_INSIST(composer.columns == 10);
        STD_INSIST(composer.pixelWidth == width);
        STD_INSIST(composer.pixelHeight == height);
    }

    // Setting it back to zero must restore exactly the geometry a window
    // that never opened a tab has, so closing back down to one session
    // cannot leave the grid a row short.
    STD_TEST(ClearingTheTopInsetRestoresTheGrid) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        StateListener listener(composer);
        composer.resizedListeners.pushBack(&listener);
        composer.setGlyphSize(8, 16);
        const u16 width = 2 * composer.opts->border + 10 * composer.glyphWidth;
        const u16 height = 2 * composer.opts->border + 4 * composer.glyphHeight;
        composer.resize(width, height);
        composer.setTopInset(composer.glyphHeight);
        const size_t afterInset = listener.calls;

        composer.setTopInset(0);

        STD_INSIST(composer.rows == 4);
        STD_INSIST(listener.calls == afterInset + 1);

        // Setting the same inset again changes nothing and must not
        // reflow: a resize walk resizes every child's tty.
        composer.setTopInset(0);
        STD_INSIST(listener.calls == afterInset + 1);
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
}
