/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "vterm_headless.h"

#include "cell_extra_store.h"
#include "composer.h"
#include "grid_geometry.h"
#include "options.h"
#include "pty.h"
#include "vterm.h"

#include <plt/fiber.h>
#include <plt/platform.h>
#include <plt/window.h>

#include <std/ios/output.h>
#include <std/lib/buffer.h>
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

    // Q2: the cell-extra store is one per window and every terminal's
    // extras live in it, but each terminal sets its budget on its own and
    // the last one to speak wins. While every terminal held the window's
    // grid that was harmless - "the last one" and "all of them" were the
    // same number. A pane is smaller, so a pane-sized budget makes the
    // store collect on behalf of the terminals that are not this one.
    //
    // The window here holds 80 x 24; the pane holds 10 x 4, which is 40
    // cells against 1920. The budget is read back through slotBudget(),
    // the only number the store publishes, and it is ten per cell.
    STD_TEST(SizesTheSharedExtraStoreByTheWindowAndNotByTheLastPane) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        VtermHeadless::create(composer, nullptr);

        const size_t windowCells = (size_t)(composer.columns) * (composer.rows + composer.opts->saveLines);
        STD_INSIST(windowCells >= (size_t)(80) * 24);
        STD_INSIST(composer.cellExtras->slotBudget() >= windowCells * 10);

        auto& panePty = *composer.pool->make<SecondPtyStub>(composer);
        Vterm* const pane = Vterm::create(*composer.pool, composer, {.columns = 10, .rows = 4}, panePty, nullptr);
        STD_INSIST(pane != nullptr);
        STD_INSIST(composer.cellExtras->slotBudget() >= windowCells * 10);

        // And a pane that shrinks does not take the store down with it:
        // paneResized is the other door into the same number.
        pane->paneResized({.columns = 8, .rows = 3});
        STD_INSIST(composer.cellExtras->slotBudget() >= windowCells * 10);
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
    STD_TEST(PointerReportsCountFromTheOriginTheVtermWasGiven) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        VtermHeadless::create(composer, nullptr);
        auto& panePty = *composer.pool->make<SecondPtyStub>(composer);
        const int glyphWidth = composer.glyphWidth;
        const int glyphHeight = composer.glyphHeight;
        const int originX = 3 * glyphWidth;
        const int originY = 2 * glyphHeight;
        Vterm* const pane = Vterm::create(*composer.pool, composer, {.columns = 10, .rows = 4, .originX = originX, .originY = originY}, panePty, nullptr);

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
        Vterm* const pane = Vterm::create(*composer.pool, composer, {.columns = 10, .rows = 4}, panePty, nullptr);
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
        pane->paneResized({.columns = 10, .rows = 4, .originX = 3 * glyphWidth, .originY = 1 * glyphHeight});
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
