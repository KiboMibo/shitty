/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "vterm_headless.h"

#include "cell_extra_store.h"
#include "composer.h"
#include "font_embedded.h"
#include "font_pack.h"
#include "font_resolver.h"
#include "grid_geometry.h"
#include "options.h"
#include "pane_layout.h"
#include "pty.h"
#include "render.h"
#include "render_reference.h"
#include "vterm.h"
#include "vterm_test.h"
#include "vterm_trace.h"

#if defined(HAVE_METAL_RENDERER)
    #include "render_metal.h"
#endif

#include <plt/fiber.h>
#include <plt/platform.h>
#include <plt/platform_headless.h>
#include <plt/window.h>

#include <std/ios/output.h>
#include <std/lib/buffer.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/tst/ut.h>

using namespace stl;

namespace {
    struct CaptureOutput final: public Output {
        size_t writeImpl(const void* data, size_t size) override;

        Buffer bytes;
    };

    static void discardOutput(Vterm& terminal) {
        if (terminal.output() != nullptr) {
            terminal.consume();
        }
    }

    static void insistMatchingCursor(Vterm& whole, Vterm& split) {
        whole.expose();
        split.expose();
        const TerminalUpdate* const wholeUpdate = whole.output();
        const TerminalUpdate* const splitUpdate = split.output();
        STD_INSIST(wholeUpdate != nullptr);
        STD_INSIST(splitUpdate != nullptr);
        STD_INSIST(wholeUpdate->cursor.posX == splitUpdate->cursor.posX);
        STD_INSIST(wholeUpdate->cursor.posY == splitUpdate->cursor.posY);
        whole.consume();
        split.consume();
    }

    // How many times `needle` appears in what the child was sent. Counted
    // rather than merely found: the whole point of the resize tests is
    // that one event produces one report, and a "contains" check passes
    // just as happily on two.
    static size_t countOccurrences(const Buffer& haystack, StringView needle) {
        const StringView bytes((const u8*)(haystack.data()), haystack.used());
        size_t found = 0;
        for (size_t at = 0; at + needle.length() <= bytes.length(); ++at) {
            if (StringView(bytes.data() + at, needle.length()) == needle) {
                ++found;
            }
        }
        return found;
    }

    static void feedInFuzzChunks(Vterm& terminal, const u8* bytes, size_t size) {
        const size_t first = bytes[0] % size;
        const size_t second = first + bytes[1] % (size - first);
        terminal.feedPty(StringView(bytes, first));
        terminal.feedPty(StringView(bytes + first, second - first));
        terminal.feedPty(StringView(bytes + second, size - second));
    }
}

size_t CaptureOutput::writeImpl(const void* data, size_t size) {
    bytes.append(data, size);
    return size;
}

namespace {
    // The second terminal of the coexistence test needs a pty face of
    // its own; test scaffolding stays in the test.
    // Vterm::create hands the trace factory the terminal's TestApi, and
    // that is the only door to advanceSelectionAutoscroll() - the forced
    // step that makes the autoscroll observable without waiting out its
    // real interval on a real loop.
    struct CaptureTestApi final: public VtermTraceFactory {
        VtermTrace* construct(TestApi* api) override {
            testApi = api;
            return nullptr;
        }

        TestApi* testApi = nullptr;
    };

    struct SecondPtyStub final: public PtyHandle {
        explicit SecondPtyStub(Composer& composer_)
            : composer(composer_)
        {
        }

        void resize(const PtySize&) override {
        }

        void engage() override {
        }

        Chunk* allocate(size_t len) override {
            payload_.reset();
            payload_.grow(len);
            payload_.seekAbsolute(len);
            used_ = len;
            return &chunk_;
        }

        void send(Chunk*, size_t len) override {
            sent.append(payload_.data(), len);
        }

        Chunk* acquire() override {
            composer.platform->scheduler()->current()->park();
            return nullptr;
        }

        void release(Chunk*) override {
        }

        struct StubChunk final: public Chunk {
            explicit StubChunk(SecondPtyStub* owner_)
                : owner(owner_)
            {
            }

            void* data() override {
                return owner->payload_.mutData();
            }

            size_t length() override {
                return owner->used_;
            }

            Chunk* next() override {
                return nullptr;
            }

            SecondPtyStub* owner;
        };

        Composer& composer;
        // Everything the terminal wrote to its child, for the tests that
        // count reports rather than merely notice them.
        stl::Buffer sent;
        stl::Buffer payload_;
        size_t used_ = 0;
        StubChunk chunk_{this};
    };
}

STD_TEST_SUITE(VtermHeadless) {
    STD_TEST(InstallsMissingComposerDependencies) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());

        VtermHeadless* const headless = VtermHeadless::create(composer, nullptr);

        STD_INSIST(composer.platform != nullptr);
        STD_INSIST(composer.window != nullptr);
        STD_INSIST(composer.window->primary() != nullptr);
        STD_INSIST(composer.window->secondary() != nullptr);
        STD_INSIST(headless->terminal() != nullptr);

        // The surface it sizes is 80 columns by 24 rows at one pixel per
        // cell. Nothing downstream pins those two: every consumer of this
        // harness feeds bytes and reads output, and a grid transposed to
        // 24x80 answers all of them without complaining. Swap the two in
        // VtermHeadless::create and this is the only line that notices.
        STD_INSIST(composer.columns == 80);
        STD_INSIST(composer.rows == 24);
        STD_INSIST(composer.pixelWidth == gridPixelWidth(80, composer.contentInsets(), composer.glyphWidth));
        STD_INSIST(composer.pixelHeight == gridPixelHeight(24, composer.contentInsets(), composer.glyphHeight));
    }

    // A tab is a second terminal behind the same window. Two of them must
    // be able to exist at once on one Composer: the terminal actions are
    // claimed once for the window, not once per terminal, and each
    // terminal contributes its own node to the action's listeners.
    STD_TEST(SecondVtermCoexistsOnOneComposer) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Vterm* const first = VtermHeadless::create(composer, nullptr)->terminal();

        Vterm* const second = Vterm::create(*composer.pool, composer, windowPane(composer), *composer.pool->make<SecondPtyStub>(composer), nullptr);

        STD_INSIST(first != nullptr);
        STD_INSIST(second != nullptr);
        STD_INSIST(first != second);
    }

    // A8: the terminal's grid is the one it was handed, not the one the
    // window has. Both terminals here share a Composer whose window is 80
    // by 24; the second was created as a 10 by 4 pane and has to describe
    // itself that way to its child.
    //
    // DEC mode 2048 is the probe because it needs no option to be turned
    // on and because it names both axes in one report: 48;rows;columns;
    // height;width. The pane's two numbers are neither equal nor the
    // window's, so an implementation that read the window, or that paired
    // columns with rows, answers something else rather than accidentally
    // right.
    STD_TEST(TakesItsGridFromThePaneItWasGiven) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        CaptureOutput windowPty;
        Vterm& whole = *VtermHeadless::create(composer, nullptr, &windowPty)->terminal();
        auto& panePty = *composer.pool->make<SecondPtyStub>(composer);
        Vterm* const pane = Vterm::create(*composer.pool, composer, {.columns = 10, .rows = 4}, panePty, nullptr);

        whole.feedPty(StringView(u8"\x1b[?2048h"));
        pane->feedPty(StringView(u8"\x1b[?2048h"));

        STD_INSIST(countOccurrences(windowPty.bytes, StringView(u8"\x1b[48;24;80;24;80t")) == 1);
        STD_INSIST(countOccurrences(panePty.sent, StringView(u8"\x1b[48;4;10;4;10t")) == 1);
    }

    // A5-3 (panes-R5-arch): DECRQSS for DECSLPP asks "how many lines is
    // the page", and the page is this terminal's, not the window's. The
    // two were the same number until A8 gave the terminal its own grid,
    // and this path reaches the window through windowInfo() rather than
    // composer.rows - which is why the grep A8's acceptance criterion
    // rests on never saw it.
    //
    // esctest covers DECSLPP too, but it cannot catch this: it sets the
    // window to 27 rows and reads 27 back, and a whole-window terminal
    // answers the same either way. Only a pane shorter than its window
    // tells the two apart.
    STD_TEST(TheDecrqssPageLengthIsThePanesAndNotTheWindows) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        CaptureOutput windowPty;
        Vterm& whole = *VtermHeadless::create(composer, nullptr, &windowPty)->terminal();
        auto& panePty = *composer.pool->make<SecondPtyStub>(composer);
        Vterm* const pane = Vterm::create(*composer.pool, composer, {.columns = 10, .rows = 4}, panePty, nullptr);

        whole.feedPty(StringView(u8"\x1bP$qt\x1b\\"));
        pane->feedPty(StringView(u8"\x1bP$qt\x1b\\"));

        STD_INSIST(countOccurrences(windowPty.bytes, StringView(u8"1$r24t")) == 1);
        STD_INSIST(countOccurrences(panePty.sent, StringView(u8"1$r4t")) == 1);
        // The window's answer given to the pane - what stood here before.
        STD_INSIST(countOccurrences(panePty.sent, StringView(u8"1$r24t")) == 0);
    }

    // A9: the frame carries the grid its rows were built by, so whoever
    // walks row.cells is told how long that array is instead of assuming
    // the window's length. The window here is 80 by 24 and the pane is
    // 10 by 4: filling these in from the composer answers 80 for the
    // pane, which is 70 cells past the end of every row it hands over.
    STD_TEST(CarriesThePaneGridInTheFrameAndNotTheWindows) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Vterm& whole = *VtermHeadless::create(composer, nullptr)->terminal();
        auto& panePty = *composer.pool->make<SecondPtyStub>(composer);
        Vterm& pane = *Vterm::create(*composer.pool, composer, {.columns = 10, .rows = 4}, panePty, nullptr);

        whole.expose();
        pane.expose();
        const TerminalUpdate* const wholeUpdate = whole.output();
        const TerminalUpdate* const paneUpdate = pane.output();
        STD_INSIST(wholeUpdate != nullptr);
        STD_INSIST(paneUpdate != nullptr);

        // Both, because a pane that answered 10 by 4 while the whole
        // window also answered 10 by 4 would be a constant, not a grid.
        STD_INSIST(wholeUpdate->gridColumns == composer.columns);
        STD_INSIST(wholeUpdate->gridRows == composer.rows);
        STD_INSIST(paneUpdate->gridColumns == 10);
        STD_INSIST(paneUpdate->gridRows == 4);

        // And it is the grid of the rows in this very frame: an exposed
        // terminal damages its whole view, so the count is the height.
        STD_INSIST(paneUpdate->rowCount == paneUpdate->gridRows);
    }

    // A11: the cell-extra store is one per window and it is sized by the
    // sum over the live panes. The list of live panes is the pane tree,
    // which lives in SessionSet - so a Composer with no SessionSet, which
    // is what a headless adapter is, has no panes to sum: a terminal
    // there is the only one there is, and it sizes the store for itself.
    //
    // The window here holds 80 x 24 and the store publishes ten slots per
    // cell through slotBudget(), the only number it exposes. That a
    // second terminal on the same Composer does *not* add to this is the
    // documented limit of a set-less Composer and not the contract:
    // SessionSet::cellCapacityExcept() is what makes the sum exact, and
    // SumsTheExtraStoreBudgetOverEveryLivePane in session_ut.cpp is where
    // that is proved.
    STD_TEST(SizesTheSharedExtraStoreByThePaneWhenThereIsNoPaneList) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        VtermHeadless::create(composer, nullptr);

        STD_INSIST(composer.sessions == nullptr);
        const size_t windowCells = (size_t)(composer.columns) * (composer.rows + composer.opts->saveLines);
        STD_INSIST(windowCells >= (size_t)(80) * 24);
        STD_INSIST(composer.cellExtras->slotBudget() >= windowCells * 10);

        // A second terminal sizes the store to its own pane, because with
        // no pane list there is nothing to add it to.
        auto& panePty = *composer.pool->make<SecondPtyStub>(composer);
        Vterm* const pane = Vterm::create(*composer.pool, composer, {.columns = 10, .rows = 4}, panePty, nullptr);
        STD_INSIST(pane != nullptr);
        const size_t paneCells = (size_t)(10) * (4 + composer.opts->saveLines);
        STD_INSIST(paneCells < windowCells);
        STD_INSIST(composer.cellExtras->slotBudget() >= paneCells * 10);
        // And it is that pane's own count and not a leftover of the
        // window's: a budget that had simply stopped being updated would
        // still read the larger number.
        STD_INSIST(composer.cellExtras->slotBudget() < windowCells * 10);

        // paneResized is the other door into the same number.
        pane->paneResized({.columns = 8, .rows = 3});
        const size_t shrunkCells = (size_t)(8) * (3 + composer.opts->saveLines);
        STD_INSIST(composer.cellExtras->slotBudget() >= shrunkCells * 10);
    }

    // The risk A8 names: resizeGrid reflows the scrollback, rebuilds the
    // screen and reports to the child, so a second idle pass over it
    // sends a resize the shell never asked for. One window resize, one
    // report - counted, because a test that only checked the numbers were
    // right would pass on two identical reports.
    STD_TEST(ReportsOneInBandResizePerWindowResize) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        CaptureOutput pty;
        Vterm& terminal = *VtermHeadless::create(composer, nullptr, &pty)->terminal();
        terminal.feedPty(StringView(u8"\x1b[?2048h"));
        pty.bytes.reset();

        // One pixel size change, one grid change: 100 by 30 at one pixel
        // per cell.
        composer.resize(100, 30);

        STD_INSIST(countOccurrences(pty.bytes, StringView(u8"\x1b[48;")) == 1);
        STD_INSIST(countOccurrences(pty.bytes, StringView(u8"\x1b[48;30;100;30;100t")) == 1);

        // Moving the pane without resizing it: the grid rebuild is
        // skipped, the report to the child is not - that is upstream's
        // contract for an unchanged grid, and it is also where a
        // duplicated pass would show up most quietly. Still exactly one.
        pty.bytes.reset();
        terminal.paneResized({.columns = 100, .rows = 30, .originX = 7, .originY = 3});
        STD_INSIST(countOccurrences(pty.bytes, StringView(u8"\x1b[48;")) == 1);
        STD_INSIST(countOccurrences(pty.bytes, StringView(u8"\x1b[48;30;100;30;100t")) == 1);

        // And a window resize to the size it already has never reaches
        // the terminal at all: Composer filters it, so the child hears
        // nothing rather than hearing the same thing twice.
        pty.bytes.reset();
        composer.resize(100, 30);
        STD_INSIST(pty.bytes.used() == 0);
    }

    // A8, the half no test reached: originX_/originY_ are carried by a
    // real Vterm, not by a MouseGeometry built in a test. Every existing
    // origin test lives in mouse_frontend_ut, where the origin is written
    // straight into the struct - so the four call sites in vterm.cpp that
    // hand originX_/originY_ to mouseGeometry(), and the two lines of
    // paneResized() that store them, were exercised only with zero. A
    // pane that never starts anywhere makes an exchanged pair of axes,
    // or a dropped origin, answer exactly what the correct code answers.
    //
    // The probes below are offset from the pane's own corner by a
    // different number of cells on each axis, and the origin itself is
    // three cells across by two rows down, so neither exchanging the two
    // origins nor dropping them lands on the right answer.
    // PA (R7-test). The store collects over its clients, and a terminal
    // hands over both of its screens - the one on show and the alternate
    // one. Nothing checked the second: dropping frame_alt from
    // VtermImpl::collectExtras() passed all 876 tests.
    //
    // The case that matters is an alternate screen that is no longer on
    // show but still holds its cells: DECSET 47 keeps the alternate
    // buffer when it is left, so a terminal that ran vim and came back
    // still owns those refs. An implementation that asked only "which
    // screen is this terminal showing" would free them under it - the
    // same dangling read the shared store was repaired for, one level
    // down.
    STD_TEST(AnInactiveAlternateScreensCellsSurviveACollection) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Vterm& terminal = *VtermHeadless::create(composer, nullptr)->terminal();

        // Into the alternate screen, and a cluster too big to sit inline
        // in a cell, so the cell holds a ref into the shared store.
        terminal.feedPty(StringView(u8"\x1b[?47h"));
        terminal.feedPty(StringView(u8"b\xcc\x82\xcc\x83"));
        terminal.expose();
        const TerminalUpdate* const inAlt = terminal.output();
        STD_INSIST(inAlt != nullptr);
        STD_INSIST(inAlt->rowCount != 0);
        const TerminalCell* const altCell = &inAlt->rows[0].cells[0];
        // The premise: without an extra there is nothing to lose and this
        // test would pass on any code at all.
        STD_INSIST(altCell->hasExtra());
        const size_t clusterSize = composer.cellExtras->grapheme(*altCell).size();
        STD_INSIST(clusterSize == 3);
        terminal.consume();

        // Back to the primary screen. Mode 47 leaves the alternate
        // buffer alone, so those cells are still out there, held by a
        // screen nobody is looking at.
        terminal.feedPty(StringView(u8"\x1b[?47l"));
        terminal.expose();
        const TerminalUpdate* const inPrimary = terminal.output();
        STD_INSIST(inPrimary != nullptr);
        STD_INSIST(&inPrimary->rows[0].cells[0] != altCell);
        terminal.consume();

        CellExtraStore* const before = composer.cellExtras;
        Vector<TerminalCell*> none;
        before->collect(none, nullptr, 0);
        // A collection really happened: collect() publishes a
        // replacement store. Without this the test passes on a build
        // that never collects, which is the shape every "the data
        // survived" check has.
        STD_INSIST(composer.cellExtras != before);

        CellExtraStore* const store = composer.cellExtras;
        STD_INSIST(altCell->hasExtra());
        STD_INSIST(store->grapheme(*altCell).size() == clusterSize);
        STD_INSIST(store->grapheme(*altCell)[0] == 'b');
        STD_INSIST(store->grapheme(*altCell)[2] == 0x0303);
    }

    // PB (R7-test). A terminal hands the collection two things: the
    // cells of its screens, and its roots - refs it holds outside any
    // cell. The open hyperlink is one of those. Dropping it from
    // VtermImpl::collectExtras() passed all 876 tests.
    //
    // CollectionRewritesNonCellRoots next door checks that the store
    // *can* rewrite a root it is handed. That is a different sentence
    // from "the terminal hands it over", and the name covers both.
    //
    // The consequence is not a cell that reads wrong now, but every cell
    // written after the collection: they are stamped with the root, and
    // a root left pointing into the dead store stamps them with whatever
    // moved into that slot.
    STD_TEST(TheOpenHyperlinkSurvivesACollectionAndKeepsStampingCells) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Vterm& terminal = *VtermHeadless::create(composer, nullptr)->terminal();

        // A second, unreferenced link first: it dies in the collection,
        // which is what moves the surviving refs and makes a stale root
        // point at something rather than at nothing.
        terminal.feedPty(StringView(u8"\x1b]8;;https://dead.test\x1b\\"));
        terminal.feedPty(StringView(u8"\x1b]8;;\x1b\\"));
        // Now the link that stays open across the collection.
        terminal.feedPty(StringView(u8"\x1b]8;;https://live.test\x1b\\"));
        terminal.feedPty(StringView(u8"A"));
        terminal.expose();
        const TerminalUpdate* const before = terminal.output();
        STD_INSIST(before != nullptr);
        STD_INSIST(before->rowCount != 0);
        const TerminalCell* const stamped = &before->rows[0].cells[0];
        STD_INSIST(stamped->hasExtra());
        STD_INSIST(composer.cellExtras->hyperlink(*stamped) == StringView(u8"https://live.test"));
        terminal.consume();

        CellExtraStore* const previous = composer.cellExtras;
        Vector<TerminalCell*> none;
        previous->collect(none, nullptr, 0);
        STD_INSIST(composer.cellExtras != previous);
        // The premise of the whole test: the collection really moved
        // things, so a root that was not carried across is now pointing
        // at a live slot belonging to somebody else.
        STD_INSIST(composer.cellExtras->findHyperlink(StringView(u8"uri=https://dead.test")) == 0);

        // The link is still open, so the next cell the shell writes is
        // stamped with it - through the root, which is the thing under
        // test.
        terminal.feedPty(StringView(u8"B"));
        terminal.expose();
        const TerminalUpdate* const after = terminal.output();
        STD_INSIST(after != nullptr);
        CellExtraStore* const store = composer.cellExtras;
        const TerminalCell* const stampedAfter = &after->rows[0].cells[1];
        STD_INSIST(stampedAfter->hasExtra());
        STD_INSIST(store->hyperlink(*stampedAfter) == StringView(u8"https://live.test"));
        // And the cell written before it still says the same thing, so
        // the two ends of the collection agree.
        STD_INSIST(store->hyperlink(*stamped) == StringView(u8"https://live.test"));
    }

    // PC (R7-test). The terminal's second root: the hyperlink captured
    // when a grapheme cluster opened, kept so that the marks arriving
    // after it are stamped with the link the base character had. It sits
    // outside every cell for as long as the cluster is being assembled,
    // and dropping it from VtermImpl::collectExtras() passed all 876
    // tests.
    //
    // So the collection has to land *inside* a cluster - after the base
    // character and before its combining mark. That is not a contrived
    // moment: a collection runs when the store fills, which is driven by
    // the shell's output, not by cluster boundaries.
    STD_TEST(TheHyperlinkOfAnOpenClusterSurvivesACollection) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Vterm& terminal = *VtermHeadless::create(composer, nullptr)->terminal();

        // A link that nothing will reference, so the collection has a
        // slot to free and the surviving refs actually move.
        terminal.feedPty(StringView(u8"\x1b]8;;https://dead.test\x1b\\"));
        terminal.feedPty(StringView(u8"\x1b]8;;\x1b\\"));
        terminal.feedPty(StringView(u8"\x1b]8;;https://live.test\x1b\\"));
        // The base character opens a cluster and takes a copy of the
        // link with it. Nothing closes the cluster yet - and nothing
        // may: an OSC between the base character and its marks ends the
        // cluster, which is how the first version of this test came to
        // assert on a cluster that had never grown.
        terminal.feedPty(StringView(u8"b"));

        CellExtraStore* const previous = composer.cellExtras;
        Vector<TerminalCell*> none;
        previous->collect(none, nullptr, 0);
        STD_INSIST(composer.cellExtras != previous);
        // The premise: the collection moved things, so a root not
        // carried across now names a slot that belongs to somebody else.
        STD_INSIST(composer.cellExtras->findHyperlink(StringView(u8"uri=https://dead.test")) == 0);

        // The mark joins the cluster opened before the collection, and
        // the cell is rewritten with the cluster's saved link.
        terminal.feedPty(StringView(u8"\xcc\x82\xcc\x83"));
        terminal.expose();
        const TerminalUpdate* const after = terminal.output();
        STD_INSIST(after != nullptr);
        STD_INSIST(after->rowCount != 0);
        const TerminalCell* const cell = &after->rows[0].cells[0];
        CellExtraStore* const store = composer.cellExtras;
        STD_INSIST(cell->hasExtra());
        // Both halves: the cluster grew, and it kept its link.
        STD_INSIST(store->grapheme(*cell).size() == 3);
        STD_INSIST(store->hyperlink(*cell) == StringView(u8"https://live.test"));
    }

    // Audit finding 6, R7-test. Two lines in vterm.cpp pass the pane's
    // origin to the mouse frontend - selectionPoint() and
    // currentSelectionAutoscrollDirection() - and neither was executed by
    // anything: T12 put __builtin_trap() in both and the suite stayed
    // green. So this is not a weak oracle, it is no execution at all, and
    // no mutation there could ever be caught.
    //
    // The pointer tests below this one drive the *reporting* path, which
    // a shell turns on with DECSET 1000. Selection is the other path -
    // the one taken when the application is not reading the mouse - and
    // it has its own translation from pixels to a cell. Today every pane
    // starts at zero, so mixing the two coordinate systems changes
    // nothing; wave 8 is where it starts costing, and it is Q1 of wave 5
    // over again, in the mouse this time.
    STD_TEST(SelectionStartsInTheCellThePaneOwnsAndNotTheWindows) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        VtermHeadless::create(composer, nullptr);
        auto& panePty = *composer.pool->make<SecondPtyStub>(composer);
        const int glyphWidth = composer.glyphWidth;
        const int glyphHeight = composer.glyphHeight;
        const int originX = 3 * glyphWidth;
        const int originY = 2 * glyphHeight;
        Vterm* const pane = Vterm::create(*composer.pool, composer, {.columns = 10, .rows = 4, .originX = originX, .originY = originY, .width = 10 * glyphWidth, .height = 4 * glyphHeight}, panePty, nullptr);
        STD_INSIST(pane != nullptr);
        // No DECSET 1000 here on purpose: with reporting off the press
        // starts a selection instead of being sent to the child, and the
        // selection is what carries the second translation.
        pane->focus(true);
        pane->pointerPresence(true);

        const Insets insets = composer.contentInsets();
        const auto at = [&](int cellsAcross, int cellsDown, int dx, int dy) {
            return plt::PointerButtonInput{
                plt::PointerButton::Primary,
                true,
                insets.left + originX + cellsAcross * glyphWidth + dx,
                insets.top + originY + cellsDown * glyphHeight + dy,
                0,
                0.0,
            };
        };

        // Press in the middle of the pane's cell (2, 1) and drag to (5, 1).
        pane->pointerButton(at(2, 1, glyphWidth / 2, glyphHeight / 2));
        pane->pointerMotion({insets.left + originX + 5 * glyphWidth + glyphWidth / 2, insets.top + originY + 1 * glyphHeight + glyphHeight / 2, 0});

        pane->expose();
        const TerminalUpdate* const update = pane->output();
        STD_INSIST(update != nullptr);
        // The pane's own cell, counted from the pane's own corner. An
        // origin dropped on the way answers (5, 3) here - three columns
        // and two rows further in, which is exactly the origin expressed
        // in cells, and still inside this 10 x 4 grid, so the wrong answer
        // looks every bit as valid as the right one.
        STD_INSIST(update->selection.tl.x == 2);
        STD_INSIST(update->selection.tl.y == 1);
        STD_INSIST(update->selection.tl.x != 5);
        STD_INSIST(update->selection.tl.y != 3);
        // And the far end travelled the three cells the pointer did,
        // which says the same translation was applied twice and not once.
        STD_INSIST(update->selection.br.x == 5);
        STD_INSIST(update->selection.br.y == 1);
    }

    // The other of the two lines. Autoscroll asks "is the pointer past
    // the edge of my grid", and the edge is the pane's top, not the
    // window's content top. The y used here sits between the two: inside
    // the window's content box, above the pane. Honouring the origin
    // makes that "above the pane" and scrolls the view back into history;
    // dropping it makes the same pixel an ordinary row of the pane and
    // scrolls nothing.
    STD_TEST(AutoscrollMeasuresFromThePanesTopEdgeAndNotTheWindows) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Options options;
        // A scrollback, or there is nowhere for the view to scroll to and
        // scrollView() refuses whichever direction it is handed.
        options.saveLines = 200;
        composer.opts = &options;
        VtermHeadless::create(composer, nullptr);
        auto& panePty = *composer.pool->make<SecondPtyStub>(composer);
        const int glyphWidth = composer.glyphWidth;
        const int glyphHeight = composer.glyphHeight;
        const int originX = 3 * glyphWidth;
        const int originY = 2 * glyphHeight;
        CaptureTestApi trace;
        Vterm* const pane = Vterm::create(*composer.pool, composer, {.columns = 10, .rows = 4, .originX = originX, .originY = originY, .width = 10 * glyphWidth, .height = 4 * glyphHeight}, panePty, &trace);
        STD_INSIST(pane != nullptr);
        STD_INSIST(trace.testApi != nullptr);
        pane->focus(true);
        pane->pointerPresence(true);

        // Enough output to put rows into the scrollback, so scrolling the
        // view back is a thing that can happen at all.
        for (unsigned line = 0; line < 40; ++line) {
            pane->feedPty(StringView(u8"line\r\n"));
        }
        pane->expose();
        const TerminalUpdate* const settled = pane->output();
        STD_INSIST(settled != nullptr);
        STD_INSIST(settled->historyRows != 0);
        STD_INSIST(settled->viewOffset == 0);
        pane->consume();

        const Insets insets = composer.contentInsets();
        // A selection has to be running, with a button down, or the
        // direction is refused before the geometry is ever consulted.
        pane->pointerButton({plt::PointerButton::Primary, true, insets.left + originX + glyphWidth / 2, insets.top + originY + 2 * glyphHeight, 0, 0.0});
        pane->pointerMotion({insets.left + originX + 4 * glyphWidth, insets.top + originY + glyphHeight / 2, 0});
        STD_INSIST(trace.testApi->hasSelection());

        // The pixel between the two edges: below the window's content
        // top, above the pane's.
        const int between = insets.top + originY - glyphHeight / 2;
        STD_INSIST(between > insets.top);
        STD_INSIST(between <= insets.top + originY);
        pane->pointerMotion({insets.left + originX + 4 * glyphWidth, between, 0});

        // Forced rather than waited for: the production path parks a
        // fiber on a deadline, and a test that slept for it would be
        // slow and flaky at once.
        STD_INSIST(trace.testApi->advanceSelectionAutoscroll());

        pane->expose();
        const TerminalUpdate* const scrolled = pane->output();
        STD_INSIST(scrolled != nullptr);
        // Scrolled back into history. Counted from the window's top the
        // same pixel is an ordinary row of this pane, the direction is
        // zero, and the view stays where it was.
        STD_INSIST(scrolled->viewOffset != 0);
    }

    STD_TEST(PointerReportsCountFromTheOriginTheVtermWasGiven) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        VtermHeadless::create(composer, nullptr);
        auto& panePty = *composer.pool->make<SecondPtyStub>(composer);
        const int glyphWidth = composer.glyphWidth;
        const int glyphHeight = composer.glyphHeight;
        const int originX = 3 * glyphWidth;
        const int originY = 2 * glyphHeight;
        Vterm* const pane = Vterm::create(*composer.pool, composer, {.columns = 10, .rows = 4, .originX = originX, .originY = originY, .width = 10 * glyphWidth, .height = 4 * glyphHeight}, panePty, nullptr);

        // VT200 button reporting with SGR coordinates: one report per
        // press, naming the cell in one line.
        pane->feedPty(StringView(u8"\x1b[?1000h\x1b[?1006h"));
        panePty.sent.reset();

        const Insets insets = composer.contentInsets();
        const auto press = [&](int cellsAcross, int cellsDown) {
            pane->pointerButton({
                plt::PointerButton::Primary,
                true,
                insets.left + originX + cellsAcross * glyphWidth,
                insets.top + originY + cellsDown * glyphHeight,
                0,
                0.0,
            });
        };

        // The pane's own first cell is 1;1 to its child, however far into
        // the window the pane begins.
        press(0, 0);
        STD_INSIST(countOccurrences(panePty.sent, StringView(u8"\x1b[<0;1;1M")) == 1);

        // Four cells across and one down: exchanging the two origins
        // answers column 4 here, dropping them answers column 8.
        panePty.sent.reset();
        press(4, 1);
        STD_INSIST(countOccurrences(panePty.sent, StringView(u8"\x1b[<0;5;2M")) == 1);

        // And down the other axis, where dropping the origin answers row
        // 5 instead of 3.
        panePty.sent.reset();
        press(0, 2);
        STD_INSIST(countOccurrences(panePty.sent, StringView(u8"\x1b[<0;1;3M")) == 1);
    }

    // The origin does not only arrive at birth: paneResized carries it
    // too, and its two assignments were the other half with no coverage.
    // Moving the pane without changing its grid has to move every pointer
    // report with it - the same press names a different cell afterwards.
    STD_TEST(MovingThePaneMovesWhereItsPointerReportsCountFrom) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        VtermHeadless::create(composer, nullptr);
        auto& panePty = *composer.pool->make<SecondPtyStub>(composer);
        const int glyphWidth = composer.glyphWidth;
        const int glyphHeight = composer.glyphHeight;
        Vterm* const pane = Vterm::create(*composer.pool, composer, {.columns = 10, .rows = 4, .width = 10 * glyphWidth, .height = 4 * glyphHeight}, panePty, nullptr);
        pane->feedPty(StringView(u8"\x1b[?1000h\x1b[?1006h"));

        const Insets insets = composer.contentInsets();
        const int pixelX = insets.left + 5 * glyphWidth;
        const int pixelY = insets.top + 3 * glyphHeight;
        const auto press = [&]() {
            pane->pointerButton({plt::PointerButton::Primary, true, pixelX, pixelY, 0, 0.0});
        };

        panePty.sent.reset();
        press();
        STD_INSIST(countOccurrences(panePty.sent, StringView(u8"\x1b[<0;6;4M")) == 1);

        // Same grid, new origin: three cells across and one row down.
        // The two offsets differ, so an implementation that stored one of
        // them into both fields answers something else.
        pane->paneResized({.columns = 10, .rows = 4, .originX = 3 * glyphWidth, .originY = 1 * glyphHeight, .width = 10 * glyphWidth, .height = 4 * glyphHeight});
        panePty.sent.reset();
        press();
        STD_INSIST(countOccurrences(panePty.sent, StringView(u8"\x1b[<0;3;3M")) == 1);
    }

    STD_TEST(KeepsFallbackTitleForTerminalReset) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Vterm* const terminal = VtermHeadless::create(composer, nullptr)->terminal();
        const u8 reset[] = {'\x1b', 'c'};

        terminal->feedPty(StringView(reset, sizeof(reset)));

        STD_INSIST(terminal->output() != nullptr);
    }

    STD_TEST(KeepsDoubleWidthOutputIndependentOfPtyChunking) {
        // Record format is the fuzz target's [op, size, pty bytes] stream.
        // All three records are pty input; the split form mirrors main_fuzz.
        const u8 corpus[] = {
            0x00, 0x41, 0x1b, 0x23, 0x36, 0xd7, 0x31, 0x67, 0x1b, 0x5b, 0x31, 0x30,
            0x30, 0x49, 0x1b, 0x5b, 0x34, 0x37, 0x5a, 0x1b, 0x5b, 0x35, 0x38, 0x3b,
            0x35, 0x3b, 0x32, 0x33, 0x33, 0x3b, 0x32, 0x35, 0x3b, 0x36, 0x38, 0x3b,
            0x34, 0x3a, 0x35, 0x3b, 0x34, 0x38, 0x3b, 0xa4, 0x35, 0x3b, 0x34, 0x38,
            0x6d, 0x1b, 0x5b, 0x31, 0x32, 0x3b, 0x33, 0x36, 0x48, 0x1b, 0x5b, 0x33,
            0x37, 0x42, 0x00, 0x3d, 0x1b, 0x5b, 0x34, 0x3b, 0x32, 0x24, 0x70, 0x1b,
            0x48, 0x1b, 0x5b, 0x31, 0x67, 0x1b, 0x5b, 0x32, 0x37, 0x49, 0x1b, 0x5b,
            0x39, 0x30, 0x5a, 0x1b, 0x5b, 0x3f, 0x32, 0x4a, 0x1b, 0x5b, 0x33, 0x31,
            0x4c, 0x1b, 0x5b, 0x3f, 0x36, 0x39, 0x68, 0x1b, 0x5b, 0x31, 0x38, 0x3b,
            0x32, 0x38, 0x72, 0x1b, 0x5b, 0x33, 0x33, 0x3b, 0x36, 0x38, 0x73, 0x1b,
            0x3b, 0x5b, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61,
            0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0xe1, 0x61,
            0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61,
            0x0a, 0x1b, 0x5d, 0x31, 0x31, 0x30, 0x3b, 0x72, 0x51, 0x51, 0x51, 0x51,
            0x51, 0x51, 0x51, 0x51, 0x51, 0x30, 0x4a, 0x1b, 0x5b, 0x33, 0x31, 0x54,
        };
        auto wholePool = ObjPool::fromMemory();
        auto splitPool = ObjPool::fromMemory();
        Composer& wholeComposer = *wholePool->make<Composer>(wholePool.mutPtr());
        Composer& splitComposer = *splitPool->make<Composer>(splitPool.mutPtr());
        Vterm& whole = *VtermHeadless::create(wholeComposer, nullptr)->terminal();
        Vterm& split = *VtermHeadless::create(splitComposer, nullptr)->terminal();
        discardOutput(whole);
        discardOutput(split);

        size_t offset = 0;
        while (offset + 2 <= sizeof(corpus)) {
            const u8 op = corpus[offset++];
            const size_t size = corpus[offset++];
            STD_INSIST(op < 192);
            STD_INSIST(offset + size <= sizeof(corpus));
            whole.feedPty(StringView(corpus + offset, size));
            feedInFuzzChunks(split, corpus + offset, size);
            insistMatchingCursor(whole, split);
            offset += size;
        }
        STD_INSIST(offset == sizeof(corpus));
    }

    STD_TEST(KeepsUtf8GraphemeInputIndependentOfPtyChunking) {
        // Saved fuzz state: the final record splits a ZWJ sequence after a
        // wide glyph wraps into, then is discarded by, a double-width row.
        const u8 corpus[] = {
            0x68, 0x65, 0x1b, 0x5b, 0x64, 0x1b, 0x08, 0x0b, 0x1b, 0x23, 0x33, 0x31,
            0x34, 0x31, 0x3b, 0x2b, 0x58, 0x5b, 0x35, 0x38, 0x3b, 0x35,
            0x3b, 0x31, 0x31,
            0xc6, 0xc4, 0xce, 0xc7, 0xc4, 0xcc, 0xc7, 0xc4, 0x32, 0x3b, 0x31, 0x34,
            0x00, 0x06, 0x1b, 0x5b, 0x3f, 0x36, 0x39, 0x68, 0x68, 0x0e, 0x1b, 0x5b,
            0x33, 0x34, 0x3b, 0x33, 0x36, 0x73, 0x00, 0x35, 0x1b, 0x5b, 0x3f, 0x36,
            0x39, 0x68, 0x1b, 0x5b, 0x35, 0x3b, 0x33, 0x30, 0x72, 0x1b, 0x5b, 0x35,
            0x31, 0x3b, 0x36, 0x32, 0x73, 0x00, 0x04, 0x1b, 0x5b, 0x35, 0x6e, 0xb1,
            0x02, 0x8d, 0x23, 0x00, 0x55, 0x1b, 0x5b, 0x31, 0x35, 0x3b, 0x31, 0x39,
            0x48, 0x1b, 0x5b, 0x33, 0x32, 0x44,

            0x33, 0x48, 0x1b, 0x5b, 0x35, 0x31, 0x47, 0x1b, 0x5b, 0x3f, 0x36, 0x68,
            0x1b, 0x5b, 0x5b, 0x31, 0x23, 0x0f, 0x9f, 0x91, 0x80, 0x8d, 0xd0, 0x6c,
            0x68, 0x1b, 0x5b, 0x34, 0x68, 0x65, 0x1b, 0x5b, 0x64, 0x1b, 0x08, 0x0b,
            0x1b, 0x23, 0x33, 0x31, 0x34, 0x31, 0x3b, 0x2b, 0x35, 0x6c, 0x1b, 0x5b,
            0x32,
            0x30, 0x68, 0x1b, 0x23, 0x34, 0x80, 0xfe, 0x09, 0x1b, 0x5b, 0x37, 0x3b,
            0x31, 0x38, 0x33, 0x48, 0x31, 0x47, 0x1b, 0x5b, 0x3f, 0x36, 0x68, 0xf0,
            0x5b,

            0x32, 0x30, 0x68, 0xd7, 0x90, 0x0d, 0x0a, 0x00, 0x3e, 0x1b, 0x5b, 0x3f,
            0x32, 0x4b, 0x1b, 0x5b, 0x31, 0x36, 0x49, 0xf0, 0x9f, 0x91, 0xa9, 0xe2,
            0x80, 0x8d, 0xf0, 0x9f, 0x95, 0xa9, 0xe2, 0x80, 0x8d, 0xf0, 0x9f, 0x91,
            0x1b, 0x5b, 0x5b, 0x3f, 0x31, 0x51, 0x51, 0x51, 0x51, 0x51, 0x51, 0x51,
            0x30, 0x68,
        };
        auto wholePool = ObjPool::fromMemory();
        auto splitPool = ObjPool::fromMemory();
        Composer& wholeComposer = *wholePool->make<Composer>(wholePool.mutPtr());
        Composer& splitComposer = *splitPool->make<Composer>(splitPool.mutPtr());
        Vterm& whole = *VtermHeadless::create(wholeComposer, nullptr)->terminal();
        Vterm& split = *VtermHeadless::create(splitComposer, nullptr)->terminal();
        discardOutput(whole);
        discardOutput(split);

        size_t offset = 0;
        while (offset + 2 <= sizeof(corpus)) {
            const u8 op = corpus[offset++];
            const size_t size = corpus[offset++];
            STD_INSIST(op < 192);
            STD_INSIST(offset + size <= sizeof(corpus));
            whole.feedPty(StringView(corpus + offset, size));
            feedInFuzzChunks(split, corpus + offset, size);
            insistMatchingCursor(whole, split);
            offset += size;
        }
        STD_INSIST(offset == sizeof(corpus));
    }

    STD_TEST(PtyAndTerminalOutputsAreConsumedIndependently) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        CaptureOutput pty;
        Vterm& terminal = *VtermHeadless::create(composer, nullptr, &pty)->terminal();
        if (terminal.output() != nullptr) {
            terminal.consume();
        }
        const u8 input[] = {'a', 0x1b, '[', 'c'};

        terminal.feedPty(StringView(input, sizeof(input)));

        STD_INSIST(!pty.bytes.empty());
        STD_INSIST(terminal.output() != nullptr);
        pty.bytes.reset();
        STD_INSIST(pty.bytes.empty());
        STD_INSIST(terminal.output() != nullptr);
        terminal.consume();
        STD_INSIST(terminal.output() == nullptr);
    }

    STD_TEST(FeedConsumesTerminalAndPtyOutput) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        CaptureOutput pty;
        VtermHeadless* const headless = VtermHeadless::create(composer, nullptr, &pty);
        const u8 input[] = {'a', 0x1b, '[', 'c'};

        headless->feed(input, sizeof(input));

        STD_INSIST(!pty.bytes.empty());
        STD_INSIST(headless->terminal()->output() == nullptr);

        pty.bytes.reset();
        headless->feed(input, sizeof(input));

        STD_INSIST(!pty.bytes.empty());
        STD_INSIST(headless->terminal()->output() == nullptr);
    }

    STD_TEST(RawDeviceAttributesDoesNotProducePtyOutputInUtf8Mode) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        CaptureOutput pty;
        Vterm* const terminal = VtermHeadless::create(composer, nullptr, &pty)->terminal();
        const u8 rawDeviceAttributes = 0x9a;

        terminal->feedPty(StringView(&rawDeviceAttributes, 1));

        STD_INSIST(pty.bytes.empty());
        terminal->feedPty(StringView(u8"\x1bZ"));
        STD_INSIST(!pty.bytes.empty());
    }

    STD_TEST(RawDeviceAttributesWorksInSingleByteMode) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        CaptureOutput pty;
        Vterm* const terminal = VtermHeadless::create(composer, nullptr, &pty)->terminal();
        const u8 input[] = {'\x1b', '%', '@', 0x9a};

        terminal->feedPty(StringView(input, sizeof(input)));

        STD_INSIST(!pty.bytes.empty());
    }

    STD_TEST(BulkUtf8DecoderMatchesByteWiseDecoder) {
        // The whole-buffer feed decodes through placeUtf8Run, tiny feeds
        // through Utf8Decoder::pushByte.  Screens must match cell for cell
        // for every replacement-character rule and chunk-boundary split.
        const u8 directed[] =
            // Valid 2-, 3- and 4-byte sequences with edge codepoints.
            u8"A\xc3\xa9 \xe2\x82\xac \xf0\x9f\x92\xbb "
            u8"\xe0\xa0\x80 \xed\x9f\xbf \xf4\x8f\xbf\xbf Z\r\n"
            // Stray continuations: the C1 range resets grapheme input.
            u8"\x80\x9f\xa0\xbf Z\r\n"
            // Bytes that can never begin a sequence.
            u8"\xc0\xc1\xf5\xff Z\r\n"
            // Overlong, surrogate and beyond-U+10FFFF first continuations.
            u8"\xe0\x80 \xe0\x9f \xed\xa0 \xf0\x80 \xf4\x90 Z\r\n"
            // Leads truncated at every position.
            u8"\xc2Z \xe2Z \xe2\x82Z \xf0Z \xf0\x90Z \xf0\x90\x8fZ\r\n"
            // Combining, wide and joined clusters against garbage.
            u8"e\xcc\x81 \xe4\xbd\xa0 \xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb \x80\xcc\x81 Z\r\n"
            // Controls inside a pending sequence are transparent to the
            // streaming decoder: the sequence completes around them.
            u8"\xe2\x07\x82\xac \xc3\x07\xa9 \xe2\x82\x07\xac \xf0\x9f\x00\x92\xbb \xe2\x7f\x82\xac Z\r\n";

        // Deterministic garbage over the full byte range except ESC: mode
        // and charset changes are covered by directed tests elsewhere.
        u8 garbage[4096];
        u32 state = 0x2545f491;
        for (size_t index = 0; index < sizeof(garbage); ++index) {
            state = state * 747796405u + 2891336453u;
            const u8 byte = (u8)(state >> 24);
            garbage[index] = byte == 0x1b ? 0x20 : byte;
        }

        const auto compareScreens = [](Vterm& whole, Vterm& split, u16 columns) {
            whole.expose();
            split.expose();
            const TerminalUpdate* const wholeUpdate = whole.output();
            const TerminalUpdate* const splitUpdate = split.output();
            STD_INSIST(wholeUpdate != nullptr);
            STD_INSIST(splitUpdate != nullptr);
            STD_INSIST(wholeUpdate->cursor.posX == splitUpdate->cursor.posX);
            STD_INSIST(wholeUpdate->cursor.posY == splitUpdate->cursor.posY);
            STD_INSIST(wholeUpdate->rowCount == splitUpdate->rowCount);
            STD_INSIST(wholeUpdate->rowCount > 0);
            for (size_t index = 0; index < wholeUpdate->rowCount; ++index) {
                const TerminalRow& wholeRow = wholeUpdate->rows[index];
                const TerminalRow& splitRow = splitUpdate->rows[index];
                STD_INSIST(wholeRow.row == splitRow.row);
                STD_INSIST(wholeRow.lineAttribute == splitRow.lineAttribute);
                for (u16 cell = 0; cell < columns; ++cell) {
                    STD_INSIST(wholeRow.cells[cell].style == splitRow.cells[cell].style);
                    STD_INSIST(wholeRow.cells[cell].content == splitRow.cells[cell].content);
                }
            }
            whole.consume();
            split.consume();
        };

        const size_t chunkSizes[] = {1, 2, 3, 7};
        for (const size_t chunk : chunkSizes) {
            auto wholePool = ObjPool::fromMemory();
            auto splitPool = ObjPool::fromMemory();
            Composer& wholeComposer = *wholePool->make<Composer>(wholePool.mutPtr());
            Composer& splitComposer = *splitPool->make<Composer>(splitPool.mutPtr());
            Vterm& whole = *VtermHeadless::create(wholeComposer, nullptr)->terminal();
            Vterm& split = *VtermHeadless::create(splitComposer, nullptr)->terminal();
            discardOutput(whole);
            discardOutput(split);

            whole.feedPty(StringView(directed, sizeof(directed) - 1));
            for (size_t offset = 0; offset < sizeof(directed) - 1; offset += chunk) {
                const size_t length = sizeof(directed) - 1 - offset < chunk ? sizeof(directed) - 1 - offset : chunk;
                split.feedPty(StringView(directed + offset, length));
            }
            compareScreens(whole, split, wholeComposer.columns);

            whole.feedPty(StringView(garbage, sizeof(garbage)));
            for (size_t offset = 0; offset < sizeof(garbage); offset += chunk) {
                const size_t length = sizeof(garbage) - offset < chunk ? sizeof(garbage) - offset : chunk;
                split.feedPty(StringView(garbage + offset, length));
            }
            compareScreens(whole, split, wholeComposer.columns);
        }
    }
}

namespace {
    // A2/R7-2: a window with a split, where only one of its panes has
    // anything new. Both terminals are panes of their own - the harness
    // terminal is here for the platform and the window it builds, not
    // for the frame - and each paints its cell 0,0 a colour of its own,
    // so what a backend kept and what it redrew can be told apart in the
    // pixels.
    struct QuietPaneFixture {
        static constexpr u16 columns = 8;
        static constexpr u16 rows = 3;

        QuietPaneFixture() {
            composer = pool->make<Composer>(pool.mutPtr());
            VtermHeadless::create(*composer, nullptr);
            // The harness draws nothing and sizes its glyph 1x1; a
            // backend needs a real one. Embedded resolver only, so the
            // test does not depend on system fonts.
            while (!composer->fontResolvers.empty()) {
                composer->fontResolvers.popFront();
            }
            composer->fontResolvers.pushBack(createEmbeddedFontResolver(*composer));
            composer->fonts = Fontpack::create(*composer, *pool, nullptr, 0, 16);
            composer->setGlyphSize(composer->fonts->getPx(), composer->fonts->getPy());
            const Insets insets = composer->contentInsets();
            composer->resize((u16)(gridPixelWidth(columns, insets, composer->glyphWidth)), (u16)(gridPixelHeight((u16)(2 * rows), insets, composer->glyphHeight)));
            busy = Vterm::create(*composer->pool, *composer, {.columns = columns, .rows = rows}, *composer->pool->make<SecondPtyStub>(*composer), nullptr);
            quiet = Vterm::create(*composer->pool, *composer, {.columns = columns, .rows = rows}, *composer->pool->make<SecondPtyStub>(*composer), nullptr);
        }

        PixelRect topArea() const {
            return {0, 0, composer->pixelWidth, (u16)(composer->pixelHeight / 2)};
        }

        PixelRect bottomArea() const {
            const u16 half = (u16)(composer->pixelHeight / 2);
            return {0, half, composer->pixelWidth, (u16)(composer->pixelHeight - half)};
        }

        // The first frame is a reshape whatever it carries - the backend
        // retains nothing yet - so both panes damage themselves whole.
        void presentWholeFrame(Renderer& renderer) {
            busy->expose();
            quiet->expose();
            const TerminalUpdate* const busyUpdate = busy->output();
            const TerminalUpdate* const quietUpdate = quiet->output();
            STD_INSIST(busyUpdate != nullptr);
            STD_INSIST(quietUpdate != nullptr);
            STD_INSIST(busyUpdate->rowCount == rows);
            STD_INSIST(quietUpdate->rowCount == rows);
            const PaneUpdate panes[2] = {
                {topArea(), *busyUpdate},
                {bottomArea(), *quietUpdate},
            };
            STD_INSIST(renderer.update(panes, 2));
            busy->consume();
            quiet->consume();
        }

        ObjPool::Ref pool = ObjPool::fromMemory();
        Composer* composer = nullptr;
        Vterm* busy = nullptr;
        Vterm* quiet = nullptr;
    };

    static const StringView paintRed(u8"\x1b[48;2;255;0;0m ");
    static const StringView paintGreen(u8"\x1b[48;2;0;255;0m ");
    // Row 2, column 1: one row of damage in a three-row grid, which is
    // what makes the frame below a partial one.
    static const StringView paintBlueOnSecondRow(u8"\x1b[2;1H\x1b[48;2;0;0;255m ");
}

// R7-2. The frame is a list of panes and every live pane owes it an
// entry; output() has none to give for a pane with nothing to say. Until
// retainedOutput() that pane simply dropped out of the list, and a frame
// with one pane where the last had two is a reshape: both backends then
// demand every row of every pane, do not get them, and refuse - which
// only asks for the same frame again. A window with a split and one idle
// shell stopped drawing.
STD_TEST_SUITE(QuietPaneFrame) {
    STD_TEST(TheReferenceBackendTakesAFrameWhoseQuietPaneDamagedNothing) {
        QuietPaneFixture fx;
        fx.busy->feedPty(paintRed);
        fx.quiet->feedPty(paintGreen);

        Vector<u8> pixels;
        pixels.zero((size_t)(fx.composer->pixelWidth) * fx.composer->pixelHeight * 3);
        plt::HeadlessRenderTarget target;
        target.pixels = pixels.mutData();
        target.length = pixels.length();
        target.width = fx.composer->pixelWidth;
        target.height = fx.composer->pixelHeight;
        target.stride = fx.composer->pixelWidth * 3;
        ObjPool::Ref rendererPool = ObjPool::fromMemory();
        ReferenceRenderer* const renderer = ReferenceRenderer::create(*fx.composer, *rendererPool, {plt::RenderBackend::Headless, nullptr, &target});
        STD_INSIST(renderer != nullptr);

        fx.presentWholeFrame(*renderer);

        // One row of one pane changes; the other pane has nothing at all
        // to say, which is exactly what output() cannot express.
        fx.busy->feedPty(paintBlueOnSecondRow);
        const TerminalUpdate* const busyUpdate = fx.busy->output();
        STD_INSIST(busyUpdate != nullptr);
        STD_INSIST(busyUpdate->rowCount == 1);
        STD_INSIST(fx.quiet->output() == nullptr);

        const TerminalUpdate& quietUpdate = fx.quiet->retainedOutput();
        // The acceptance criterion, in its two halves: the quiet pane
        // owes the frame no rows at all, and it still names the grid its
        // retained cells were built by (A9 - zero would be a refusal).
        STD_INSIST(quietUpdate.rowCount == 0);
        STD_INSIST(quietUpdate.gridColumns == QuietPaneFixture::columns);
        STD_INSIST(quietUpdate.gridRows == QuietPaneFixture::rows);

        const PaneUpdate panes[2] = {
            {fx.topArea(), *busyUpdate},
            {fx.bottomArea(), quietUpdate},
        };
        STD_INSIST(renderer->update(panes, 2));

        const ReferenceImage image = renderer->image();
        STD_INSIST(image.pixels != nullptr);
        const Insets insets = fx.composer->paneInsets();
        const auto pixelAt = [&image](u16 x, u16 y) {
            const size_t index = 3 * ((size_t)(y)*image.width + x);
            return Color{image.pixels[index], image.pixels[index + 1], image.pixels[index + 2]};
        };
        // Kept, not repainted: the quiet pane's cell 0,0 is the green it
        // was given a frame ago, and this frame carried no cell of it.
        const PixelRect bottom = fx.bottomArea();
        STD_INSIST((pixelAt((u16)(bottom.x + insets.left), (u16)(bottom.y + insets.top)) == Color{0, 255, 0}));
        // And the busy pane's one damaged row did land.
        const PixelRect top = fx.topArea();
        STD_INSIST((pixelAt((u16)(top.x + insets.left), (u16)(top.y + insets.top + fx.composer->glyphHeight)) == Color{0, 0, 255}));

        // The regression this exists for. Drop the quiet pane from the
        // frame - which is all the layout could do before this method -
        // and the frame is refused, because a pane count that changed is
        // a reshape and the busy pane damaged one row of three. The
        // refusal asks for the frame again, and the next one is the same
        // one: the window is stuck here.
        const PaneUpdate alone[1] = {{{0, 0, fx.composer->pixelWidth, fx.composer->pixelHeight}, *busyUpdate}};
        STD_INSIST(!renderer->update(alone, 1));
    }

    // Vterm's own half of the contract, without a backend in the way:
    // the retained form is the update output() would have given, minus
    // the damage - and it takes none of the damage with it, so the
    // output() that follows still reports it whole.
    STD_TEST(TheRetainedFormCarriesThePresentationAndLeavesTheDamageAlone) {
        QuietPaneFixture fx;
        fx.busy->feedPty(paintRed);
        discardOutput(*fx.busy);
        fx.busy->feedPty(paintBlueOnSecondRow);

        const TerminalUpdate& retained = fx.busy->retainedOutput();
        STD_INSIST(retained.rowCount == 0);
        STD_INSIST(retained.colors != nullptr);
        STD_INSIST(retained.shapes != nullptr);
        STD_INSIST(retained.cursor.posY == 1);

        // Asking for it neither consumed the pending row nor armed
        // consume(): the frame that follows is the one that was owed.
        const TerminalUpdate* const update = fx.busy->output();
        STD_INSIST(update != nullptr);
        STD_INSIST(update->rowCount == 1);
        STD_INSIST(update->rows[0].row == 1);
        STD_INSIST(update->shapes == retained.shapes);
        STD_INSIST(update->gridColumns == retained.gridColumns);
        STD_INSIST(update->gridRows == retained.gridRows);
        fx.busy->consume();

        // Consumed, so there is nothing left to say - and the retained
        // form is still there to say it.
        STD_INSIST(fx.busy->output() == nullptr);
        STD_INSIST(fx.busy->retainedOutput().rowCount == 0);
    }
}

#if defined(HAVE_METAL_RENDERER)

// The same frame at the other backend. Both of them retain cells across
// frames and both reshape on a changed pane count, so a quiet pane that
// only one of them accepted would be a pane that cannot be drawn on the
// hardware path.
STD_TEST_SUITE(QuietPaneFrameOnMetal) {
    STD_TEST(TheMetalBackendTakesAFrameWhoseQuietPaneDamagedNothing) {
        QuietPaneFixture fx;
        fx.busy->feedPty(paintRed);
        fx.quiet->feedPty(paintGreen);

        ObjPool::Ref rendererPool = ObjPool::fromMemory();
        Renderer* const renderer = createMetalRenderer(*fx.composer, *rendererPool, {plt::RenderBackend::Headless, nullptr, nullptr});
        STD_INSIST(renderer != nullptr);

        fx.presentWholeFrame(*renderer);

        fx.busy->feedPty(paintBlueOnSecondRow);
        const TerminalUpdate* const busyUpdate = fx.busy->output();
        STD_INSIST(busyUpdate != nullptr);
        STD_INSIST(busyUpdate->rowCount == 1);
        STD_INSIST(fx.quiet->output() == nullptr);

        const TerminalUpdate& quietUpdate = fx.quiet->retainedOutput();
        STD_INSIST(quietUpdate.rowCount == 0);
        STD_INSIST(quietUpdate.gridColumns == QuietPaneFixture::columns);
        STD_INSIST(quietUpdate.gridRows == QuietPaneFixture::rows);

        const PaneUpdate panes[2] = {
            {fx.topArea(), *busyUpdate},
            {fx.bottomArea(), quietUpdate},
        };
        STD_INSIST(renderer->update(panes, 2));

        Buffer rgb;
        u32 width = 0;
        u32 height = 0;
        STD_INSIST(renderer->captureOutput(rgb, width, height));
        STD_INSIST(width == fx.composer->pixelWidth);
        const Insets insets = fx.composer->paneInsets();
        const auto pixelAt = [&rgb, width](u16 x, u16 y) {
            const auto* const bytes = (const u8*)(rgb.data());
            const size_t index = 3 * ((size_t)(y)*width + x);
            return Color{bytes[index], bytes[index + 1], bytes[index + 2]};
        };
        const PixelRect bottom = fx.bottomArea();
        STD_INSIST((pixelAt((u16)(bottom.x + insets.left), (u16)(bottom.y + insets.top)) == Color{0, 255, 0}));
        const PixelRect top = fx.topArea();
        STD_INSIST((pixelAt((u16)(top.x + insets.left), (u16)(top.y + insets.top + fx.composer->glyphHeight)) == Color{0, 0, 255}));

        const PaneUpdate alone[1] = {{{0, 0, fx.composer->pixelWidth, fx.composer->pixelHeight}, *busyUpdate}};
        STD_INSIST(!renderer->update(alone, 1));
    }
}

#endif
