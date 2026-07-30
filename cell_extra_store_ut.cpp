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

    CellExtraStore* createStore(Composer& composer, size_t cellCount) {
        CellExtraStore* const store = CellExtraStore::create(composer, cellCount);
        composer.setCellExtras(store);
        return store;
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
    STD_TEST(FactoryDoesNotWireComposer) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        CellExtraStore* const store = CellExtraStore::create(composer, 1);

        STD_INSIST(store != nullptr);
        STD_INSIST(composer.cellExtras == nullptr);
    }

    STD_TEST(KeepsInlineUnderlineColorWithoutExtra) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        CellExtraStore& store = *createStore(composer, 1);
        TerminalCell cell{};
        const CellColor color = CellColor::direct({17, 34, 51});

        store.setUnderlineColor(cell, color);

        STD_INSIST(!cell.hasExtra());
        STD_INSIST(store.underlineColor(cell) == color);
    }

    STD_TEST(CombinesAndClearsIndependentExtraValues) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        CellExtraStore& store = *createStore(composer, 1);
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

    STD_TEST(MulticellSharesOnePayloadAcrossBands) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        CellExtraStore& store = *createStore(composer, 6);
        const u32 text[] = {'A', 0x0301, 'B'};
        const MulticellSpec spec{
            .columns = 3,
            .rows = 2,
            .scale = 2,
            .width = 0,
            .numerator = 1,
            .denominator = 2,
            .verticalAlignment = 1,
            .horizontalAlignment = 2,
        };
        MulticellHandle* const handle = store.createMulticell(text, 3, spec);
        TerminalCell head{};
        TerminalCell band{};

        store.setMulticell(head, handle, 0, 0);
        store.setMulticell(band, handle, 2, 1);

        const MulticellView first = store.multicell(head);
        const MulticellView second = store.multicell(band);
        STD_INSIST(first.valid());
        STD_INSIST(first.head());
        STD_INSIST(first.identity == second.identity);
        STD_INSIST(second.column == 2);
        STD_INSIST(second.row == 1);
        STD_INSIST(second.spec.columns == 3);
        STD_INSIST(second.spec.rows == 2);
        STD_INSIST(second.spec.scale == 2);
        STD_INSIST(second.spec.numerator == 1);
        STD_INSIST(second.spec.denominator == 2);
        STD_INSIST(second.text.size() == 3);
        STD_INSIST(second.text[1] == 0x0301);
        STD_INSIST(store.grapheme(head).size() == 3);
        STD_INSIST(store.grapheme(band).empty());
        STD_INSIST(store.view(band).grapheme.size() == 3);
    }

    STD_TEST(CollectionPreservesSharedMulticellPayload) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        CellExtraStore* store = createStore(composer, 2);
        const u32 text[] = {0x1f642};
        const MulticellSpec spec{
            .columns = 2,
            .rows = 1,
            .scale = 1,
        };
        MulticellHandle* const handle = store->createMulticell(text, 1, spec);
        TerminalCell first{};
        TerminalCell second{};
        store->setMulticell(first, handle, 0, 0);
        store->setMulticell(second, handle, 1, 0);
        Vector<TerminalCell*> cells;
        cells.pushBack(&first);
        cells.pushBack(&second);

        store->collect(cells, nullptr, 0);
        store = composer.cellExtras;

        const MulticellView migratedFirst = store->multicell(first);
        const MulticellView migratedSecond = store->multicell(second);
        STD_INSIST(migratedFirst.valid());
        STD_INSIST(migratedFirst.identity == migratedSecond.identity);
        STD_INSIST(migratedFirst.text.size() == 1);
        STD_INSIST(migratedFirst.text[0] == 0x1f642);
        STD_INSIST(migratedSecond.column == 1);
    }

    STD_TEST(ReusesHyperlinkIdentity) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        CellExtraStore& store = *createStore(composer, 1);
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
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        ExtraChangeListener listener(composer);
        composer.cellExtrasChangedListeners.pushBack(&listener);
        CellExtraStore* store = createStore(composer, 2);
        TerminalCell cell{};
        const u32 grapheme[] = {0x1f469, 0x200d, 0x1f4bb};
        const u32 hyperlink = store->getOrCreateHyperlink(StringView(u8"live"), StringView(u8"https://live.test"), 19);
        store->setGrapheme(cell, grapheme, 3);
        store->setHyperlink(cell, hyperlink);
        Vector<TerminalCell*> cells;
        cells.pushBack(&cell);
        CellExtraStore* const previous = store;
        const size_t notificationsBefore = listener.calls;

        store->collect(cells, nullptr, 0);
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
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        CellExtraStore* store = createStore(composer, 1);
        const u32 live = store->getOrCreateHyperlink(StringView(u8"live"), StringView(u8"https://live.test"), 1);
        store->getOrCreateHyperlink(StringView(u8"dead"), StringView(u8"https://dead.test"), 2);
        TerminalCell cell{};
        store->setHyperlink(cell, live);
        Vector<TerminalCell*> cells;
        cells.pushBack(&cell);

        store->collect(cells, nullptr, 0);
        store = composer.cellExtras;

        STD_INSIST(store->hyperlinkCount() == 1);
        STD_INSIST(store->findHyperlink(StringView(u8"live")) != 0);
        STD_INSIST(store->findHyperlink(StringView(u8"dead")) == 0);
    }

    STD_TEST(CollectionPreservesDistinctRefs) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        CellExtraStore* store = createStore(composer, 2);
        TerminalCell first{};
        TerminalCell second{};
        const u32 grapheme[] = {'x', 0x0301};
        store->setGrapheme(first, grapheme, 2);
        store->setGrapheme(second, grapheme, 2);
        Vector<TerminalCell*> cells;
        cells.pushBack(&first);
        cells.pushBack(&second);

        STD_INSIST(first.extraRef() != second.extraRef());

        store->collect(cells, nullptr, 0);

        STD_INSIST(first.extraRef() != second.extraRef());
    }

    STD_TEST(CollectionRewritesNonCellRoots) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        CellExtraStore* store = createStore(composer, 1);
        store->getOrCreateHyperlink(StringView(u8"dead"), StringView(u8"https://dead.test"), 1);
        u32 root = store->getOrCreateHyperlink(StringView(u8"live"), StringView(u8"https://live.test"), 2);
        u32* roots[] = {&root};
        Vector<TerminalCell*> cells;

        store->collect(cells, roots, 1);
        store = composer.cellExtras;

        TerminalCell cell{};
        store->setHyperlink(cell, root);
        STD_INSIST(root == 1);
        STD_INSIST(store->findHyperlink(StringView(u8"dead")) == 0);
        STD_INSIST(store->findHyperlink(StringView(u8"live")) == root);
        STD_INSIST(store->hyperlink(cell) == StringView(u8"https://live.test"));
    }

    STD_TEST(ReportsAllocationPressure) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        CellExtraStore& store = *createStore(composer, 1);
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
