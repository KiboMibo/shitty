/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "cell_extra_store.h"

#include "composer.h"
#include "listener.h"

#include <std/mem/obj_pool.h>
#include <std/tst/ut.h>

using namespace stl;

namespace {
    struct ExtraChangeListener final: public Listener {
        explicit ExtraChangeListener(Composer& composer);

        void onListen(void*) override;

        Composer& composer;
        CellExtraStore* observed = nullptr;
        size_t calls = 0;
    };

    bool equal(StringView left, StringView right) {
        return left == right;
    }
}

ExtraChangeListener::ExtraChangeListener(Composer& composer_)
    : composer(composer_)
{
}

void ExtraChangeListener::onListen(void*) {
    observed = composer.cellExtras;
    ++calls;
}

STD_TEST_SUITE(CellExtraStore) {
    STD_TEST(KeepsInlineUnderlineColorWithoutExtra) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CellExtraStore& store = *CellExtraStore::create(composer, 1);
        TerminalCell cell{};
        const CellColor color = CellColor::direct({17, 34, 51});

        store.setUnderlineColor(cell, color);

        STD_INSIST(!cell.hasExtra());
        STD_INSIST(store.underlineColor(cell) == color);
    }

    STD_TEST(CombinesAndClearsIndependentExtraValues) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CellExtraStore& store = *CellExtraStore::create(composer, 1);
        TerminalCell cell{};
        const CellColor underline = CellColor::indexed(42);
        const u32 grapheme[] = {'a', 0x0301};
        const u32 hyperlink = store.getOrCreateHyperlink(StringView(u8"id"), StringView(u8"https://example.test"), 73);

        store.setUnderlineColor(cell, underline);
        store.setGrapheme(cell, grapheme, 2);
        store.setHyperlink(cell, hyperlink);

        const GraphemeView stored = store.grapheme(cell);
        STD_INSIST(cell.hasExtra());
        STD_INSIST(store.underlineColor(cell) == underline);
        STD_INSIST(stored.size() == 2);
        STD_INSIST(stored[0] == grapheme[0]);
        STD_INSIST(stored[1] == grapheme[1]);
        STD_INSIST(equal(store.hyperlink(cell), StringView(u8"https://example.test")));
        STD_INSIST(store.hyperlinkDisplayId(cell) == 73);

        store.clearGrapheme(cell);

        STD_INSIST(store.grapheme(cell).empty());
        STD_INSIST(equal(store.hyperlink(cell), StringView(u8"https://example.test")));
        STD_INSIST(store.underlineColor(cell) == underline);

        store.clearHyperlink(cell);

        STD_INSIST(!cell.hasExtra());
        STD_INSIST(store.hyperlink(cell).empty());
        STD_INSIST(store.underlineColor(cell) == underline);
    }

    STD_TEST(ReusesHyperlinkIdentity) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CellExtraStore& store = *CellExtraStore::create(composer, 1);
        const u32 first = store.getOrCreateHyperlink(StringView(u8"same"), StringView(u8"first"), 1);
        const u32 second = store.getOrCreateHyperlink(StringView(u8"same"), StringView(u8"second"), 2);
        TerminalCell cell{};
        store.setHyperlink(cell, second);

        STD_INSIST(first == second);
        STD_INSIST(store.findHyperlink(StringView(u8"same")) == first);
        STD_INSIST(store.hyperlinkCount() == 1);
        STD_INSIST(equal(store.hyperlink(cell), StringView(u8"first")));
        STD_INSIST(store.hyperlinkDisplayId(cell) == 1);
    }

    STD_TEST(CollectionRebuildsStoreAndPreservesLiveValues) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        ExtraChangeListener listener(composer);
        composer.cellExtrasChangedListeners.pushBack(&listener);
        CellExtraStore* store = CellExtraStore::create(composer, 2);
        TerminalCell cell{};
        const u32 grapheme[] = {0x1f469, 0x200d, 0x1f4bb};
        const u32 hyperlink = store->getOrCreateHyperlink(StringView(u8"live"), StringView(u8"https://live.test"), 19);
        store->setGrapheme(cell, grapheme, 3);
        store->setHyperlink(cell, hyperlink);
        Vector<u32*> locations;
        locations.pushBack(&cell.payload);
        CellExtraStore* const previous = store;
        const size_t notificationsBefore = listener.calls;

        store->collect(locations);
        store = composer.cellExtras;

        STD_INSIST(store != previous);
        STD_INSIST(listener.calls == notificationsBefore + 1);
        STD_INSIST(listener.observed == store);
        STD_INSIST(cell.hasExtra());
        STD_INSIST(store->grapheme(cell).size() == 3);
        STD_INSIST(store->grapheme(cell)[2] == grapheme[2]);
        STD_INSIST(equal(store->hyperlink(cell), StringView(u8"https://live.test")));
        STD_INSIST(store->hyperlinkDisplayId(cell) == 19);
        STD_INSIST(store->findHyperlink(StringView(u8"live")) != 0);
    }

    STD_TEST(CollectionDropsUnreachableHyperlinks) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CellExtraStore* store = CellExtraStore::create(composer, 1);
        const u32 live = store->getOrCreateHyperlink(StringView(u8"live"), StringView(u8"https://live.test"), 1);
        store->getOrCreateHyperlink(StringView(u8"dead"), StringView(u8"https://dead.test"), 2);
        TerminalCell cell{};
        store->setHyperlink(cell, live);
        Vector<u32*> locations;
        locations.pushBack(&cell.payload);

        store->collect(locations);
        store = composer.cellExtras;

        STD_INSIST(store->hyperlinkCount() == 1);
        STD_INSIST(store->findHyperlink(StringView(u8"live")) != 0);
        STD_INSIST(store->findHyperlink(StringView(u8"dead")) == 0);
    }

    STD_TEST(CollectionPreservesDistinctRefs) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CellExtraStore* store = CellExtraStore::create(composer, 2);
        TerminalCell first{};
        TerminalCell second{};
        const u32 grapheme[] = {'x', 0x0301};
        store->setGrapheme(first, grapheme, 2);
        store->setGrapheme(second, grapheme, 2);
        Vector<u32*> locations;
        locations.pushBack(&first.payload);
        locations.pushBack(&second.payload);

        STD_INSIST(first.extraRef() != second.extraRef());

        store->collect(locations);

        STD_INSIST(first.extraRef() != second.extraRef());
    }

    STD_TEST(ReportsAllocationPressure) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CellExtraStore& store = *CellExtraStore::create(composer, 1);
        TerminalCell cell{};

        for (u32 codepoint = 1; codepoint <= 17; ++codepoint) {
            store.setGrapheme(cell, &codepoint, 1);
        }

        STD_INSIST(store.shouldCollect());
        STD_INSIST(!store.hardLimitExceeded());

        for (u32 codepoint = 18; codepoint <= 33; ++codepoint) {
            store.setGrapheme(cell, &codepoint, 1);
        }

        STD_INSIST(store.hardLimitExceeded());
    }
}
