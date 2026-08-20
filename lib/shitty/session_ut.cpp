/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "session.h"

#include "cell_extra_store.h"
#include "composer.h"
#include "input_handler.h"
#include "listener.h"
#include "options.h"
#include "pane_layout.h"
#include "pty.h"
#include "startup.h"
#include "vterm.h"

#include <plt/fiber.h>
#include <plt/platform.h>
#include <plt/platform_headless.h>
#include <plt/poller.h>
#include <plt/poller_loop.h>

#include <std/ios/input.h>
#include <std/ios/out.h>
#include <std/ios/output.h>
#include <std/lib/buffer.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/mem/small_obj_allocator.h>
#include <std/tst/ut.h>

using namespace stl;

namespace {
    // A trivially owned chunk for the stub: header and payload in one
    // small-obj allocation, released on send.
    struct StubChunk final: public PtyHandle::Chunk, public stl::Newable {
        void* data() override {
            return this + 1;
        }

        size_t length() override {
            return used;
        }

        Chunk* next() override {
            return nullptr;
        }

        SmallObjAllocator* owner = nullptr;
        u32 allocated = 0;
        u32 used = 0;
    };

    struct StubHandle final: public PtyHandle {
        StubHandle(Composer& composer_, size_t* destroyed_, bool* writeEntered, bool* writeResumed)
            : composer(composer_)
            , destroyed(destroyed_)
            , entered(writeEntered)
            , resumed(writeResumed)
        {
        }

        ~StubHandle() noexcept {
            ++*destroyed;
        }

        void resize(const PtySize& requested) override {
            size = requested;
            ++resizes;
        }

        void engage() override {
        }

        Chunk* allocate(size_t len) override {
            constexpr size_t cap = smallObjMaxSize - sizeof(StubChunk);
            const size_t granted = len < cap ? len : cap;
            auto* const chunk = new (composer.smallObjects->allocate(sizeof(StubChunk) + granted)) StubChunk;
            chunk->owner = composer.smallObjects;
            chunk->allocated = (u32)(sizeof(StubChunk) + granted);
            chunk->used = (u32)(granted);
            return chunk;
        }

        void send(Chunk* chunk, size_t len) override {
            auto* const block = static_cast<StubChunk*>(chunk);
            written.append((const char*)(block->data()), len);
            if (entered != nullptr) {
                *entered = true;
                composer.platform->scheduler()->current()->park();
                *resumed = true;
            }
            block->owner->deallocate(block, block->allocated);
        }

        // Parks the reading fiber the way a live pty does between
        // chunks, and - unlike the version that parked forever - can be
        // told the child is gone. That is the only door into
        // SessionSet's EOF path: PtyReadBody turns a null acquire() into
        // ptyEof(), which is private and reachable no other way.
        Chunk* acquire() override {
            for (;;) {
                if (atEof) {
                    return nullptr;
                }
                reader = composer.platform->scheduler()->current();
                reader->park();
            }
        }

        // Called from the test, off the fiber: wake() from the platform
        // thread is remembered even if the fiber is between parks.
        void reportEof() {
            atEof = true;
            if (reader != nullptr) {
                reader->wake();
            }
        }

        void release(Chunk*) override {
        }

        Composer& composer;
        size_t* destroyed;
        bool* entered;
        bool* resumed;
        PtySize size{};
        size_t resizes = 0;
        plt::Fiber* reader = nullptr;
        bool atEof = false;
        // What this session's child was told, which is the only place a
        // focus report can be observed from outside the terminal.
        Buffer written;
    };

    struct StubPty final: public Pty {
        StubPty(Composer& composer_, size_t& destroyed_)
            : composer(composer_)
            , destroyed(destroyed_)
        {
        }

        PtyHandle* spawn(ObjPool& owner, const LaunchCommand&) override {
            StubHandle* const handle = owner.make<StubHandle>(
                composer,
                &destroyed,
                blockNextWrite ? &writeEntered : nullptr,
                blockNextWrite ? &writeResumed : nullptr
            );
            blockNextWrite = false;
            handles.pushBack(handle);
            return handle;
        }

        Composer& composer;
        Vector<StubHandle*> handles;
        size_t& destroyed;
        bool blockNextWrite = false;
        bool writeEntered = false;
        bool writeResumed = false;
    };

    constexpr u64 testTimeoutUs = 5'000'000;

    struct Timeout final: public plt::TimerCallback {
        void ready() override {
            fired = true;
        }

        bool fired = false;
    };

    void publish(IntrusiveList& listeners) {
        for (IntrusiveNode* node = listeners.mutFront(); node != listeners.mutEnd();) {
            Listener* const listener = static_cast<Listener*>(node);
            node = node->next;
            listener->onListen();
        }
    }

    struct Harness {
        // saveLines is a constructor argument because the first session
        // is opened here: a scrollback set afterwards would miss the
        // screen it is supposed to size.
        // The glyph is square and one pixel by default, which is what lets
        // most tests read a pixel count as a cell count. A test about the
        // two axes has to say otherwise: see
        // ThePixelSizeEachShellIsToldPairsEachAxisWithItsOwnGlyph.
        explicit Harness(size_t* destroyed = nullptr, u16 saveLines = 0, u16 border = 0, u16 glyphWidth = 1, u16 glyphHeight = 1)
            : composer(*pool->make<Composer>(pool.mutPtr()))
            , pty(composer, destroyed == nullptr ? ownedDestroyed : *destroyed) {
            options.saveLines = saveLines;
            options.border = border;
            composer.platform = plt::createHeadlessPlatform(*composer.pool);
            composer.window = composer.platform->createWindow(*composer.pool, {.width = 80, .height = 24});
            composer.setGlyphSize(glyphWidth, glyphHeight);
            composer.opts = &options;
            composer.resize(80, 24);
            composer.pty = &pty;
            composer.launch = &command;
            sessions = SessionSet::create(composer);
        }

        void newTab() {
            publish(composer.newTabListeners);
        }

        void closeTab() {
            publish(composer.closeTabListeners);
        }

        void nextTab() {
            publish(composer.nextTabListeners);
        }

        // The window gaining or losing focus, delivered down the handler
        // chain exactly as the platform delivers it.
        void windowFocus(bool focused) {
            for (IntrusiveNode* node = composer.inputHandlers.mutFront(); node != composer.inputHandlers.mutEnd(); node = node->next) {
                static_cast<InputHandler*>(node)->focus(focused);
            }
        }

        void previousTab() {
            publish(composer.prevTabListeners);
        }

        void splitVertical() {
            publish(composer.splitVerticalListeners);
        }

        void splitHorizontal() {
            publish(composer.splitHorizontalListeners);
        }

        // A key, delivered down the handler chain exactly as the
        // platform delivers it - so it passes the binding table first and
        // reaches whichever pane the set says is focused.
        void keyPress(plt::InputKey key, u16 modifiers = 0, u32 baseCodepoint = 0) {
            const plt::KeyInput input{key, plt::InputAction::Press, modifiers, 0, baseCodepoint};
            for (IntrusiveNode* node = composer.inputHandlers.mutFront(); node != composer.inputHandlers.mutEnd(); node = node->next) {
                if (static_cast<InputHandler*>(node)->key(input)) {
                    return;
                }
            }
        }

        size_t ownedDestroyed = 0;
        ObjPool::Ref pool = ObjPool::fromMemory();
        // Panes are behind an option and off by default, so a harness
        // that wants them has to say so - which is itself the check that
        // splitFocused() refuses when the option is off.
        Options options;
        Composer& composer;
        LaunchCommand command;
        StubPty pty;
        SessionSet* sessions = nullptr;
    };
}

namespace {
    struct ModelProbe final: public Listener {
        explicit ModelProbe(SessionSet* sessions_);

        void onListen(void*) override;

        SessionSet* sessions;
        size_t count = 0;
        size_t active = 0;
        unsigned notified = 0;
    };
}

ModelProbe::ModelProbe(SessionSet* sessions_)
    : sessions(sessions_)
{
}

void ModelProbe::onListen(void*) {
    count = sessions->count();
    active = sessions->activeIndex();
    ++notified;
}

STD_TEST_SUITE(SessionSet) {
    STD_TEST(TabModelCommitsBeforeItNotifies) {
        Harness harness;
        ModelProbe probe{harness.sessions};
        harness.composer.sessionsChangedListeners.pushBack(&probe);

        harness.newTab();
        STD_INSIST(probe.notified != 0);
        STD_INSIST(probe.count == 2);
        STD_INSIST(probe.active == 1);
        STD_INSIST(harness.sessions->title(0).length() == 0);

        harness.sessions->activate(0);
        STD_INSIST(probe.active == 0);

        harness.closeTab();
        STD_INSIST(probe.count == 1);
        STD_INSIST(probe.active == 0);
    }

    STD_TEST(DirectSelectionChordsPickTheirTab) {
        Harness harness;
        harness.newTab();
        harness.newTab();
        STD_INSIST(harness.sessions->activeIndex() == 2);

        publish(harness.composer.selectTabListeners[0]);
        STD_INSIST(harness.sessions->activeIndex() == 0);

        // The ninth chord means "the last tab" however many there are.
        publish(harness.composer.selectTabListeners[8]);
        STD_INSIST(harness.sessions->activeIndex() == 2);

        // Out-of-range and already-active chords change nothing.
        publish(harness.composer.selectTabListeners[6]);
        STD_INSIST(harness.sessions->activeIndex() == 2);
        publish(harness.composer.selectTabListeners[2]);
        STD_INSIST(harness.sessions->activeIndex() == 2);
    }

    STD_TEST(ClosingABackgroundTabKeepsTheViewPut) {
        Harness harness;
        ModelProbe probe{harness.sessions};
        harness.composer.sessionsChangedListeners.pushBack(&probe);

        harness.newTab();
        harness.newTab();
        STD_INSIST(harness.sessions->activeIndex() == 2);
        Vterm* const watched = harness.sessions->activeTerminal();

        STD_INSIST(harness.sessions->close(0));
        STD_INSIST(harness.sessions->activeTerminal() == watched);
        STD_INSIST(probe.count == 2);
        STD_INSIST(probe.active == 1);

        STD_INSIST(harness.sessions->close(0));
        STD_INSIST(harness.sessions->activeTerminal() == watched);
        STD_INSIST(probe.count == 1);
        STD_INSIST(probe.active == 0);
    }

    STD_TEST(CreateOpensAndActivatesTheFirstSession) {
        Harness harness;

        STD_INSIST(SessionSet::liveSessions == 1);
        STD_INSIST(harness.pty.handles.length() == 1);
        STD_INSIST(harness.sessions->activeTerminal() != nullptr);
        STD_INSIST(harness.pty.handles[0]->resizes == 1);
    }

    STD_TEST(NewTabUsesTheSameProductionSpawnPath) {
        Harness harness;
        Vterm* const first = harness.sessions->activeTerminal();

        harness.newTab();

        STD_INSIST(SessionSet::liveSessions == 2);
        STD_INSIST(harness.pty.handles.length() == 2);
        STD_INSIST(harness.sessions->activeTerminal() != first);
    }

    STD_TEST(NextAndPreviousWrapAround) {
        Harness harness;
        Vterm* const first = harness.sessions->activeTerminal();
        harness.newTab();
        Vterm* const second = harness.sessions->activeTerminal();
        harness.newTab();
        Vterm* const third = harness.sessions->activeTerminal();

        harness.nextTab();
        STD_INSIST(harness.sessions->activeTerminal() == first);
        harness.nextTab();
        STD_INSIST(harness.sessions->activeTerminal() == second);
        harness.previousTab();
        STD_INSIST(harness.sessions->activeTerminal() == first);
        harness.previousTab();
        STD_INSIST(harness.sessions->activeTerminal() == third);
    }

    STD_TEST(SwitchingOneSessionStaysPut) {
        Harness harness;
        Vterm* const only = harness.sessions->activeTerminal();

        harness.nextTab();
        harness.previousTab();

        STD_INSIST(harness.sessions->activeTerminal() == only);
    }

    STD_TEST(CloseTabDestroysItsWholeArena) {
        Harness harness;
        Vterm* const first = harness.sessions->activeTerminal();
        harness.newTab();

        harness.closeTab();

        STD_INSIST(SessionSet::liveSessions == 1);
        STD_INSIST(harness.pty.destroyed == 1);
        STD_INSIST(harness.sessions->activeTerminal() == first);
    }

    STD_TEST(ClosingTheLastSessionReportsNoLiveSessions) {
        Harness harness;

        harness.closeTab();

        STD_INSIST(SessionSet::liveSessions == 0);
    }

    STD_TEST(TeardownReleasesEverySessionArena) {
        size_t destroyed = 0;
        {
            Harness harness(&destroyed);
            harness.newTab();
        }

        STD_INSIST(destroyed == 2);
        STD_INSIST(SessionSet::liveSessions == 0);
    }

    STD_TEST(ResizeReachesEverySessionHandle) {
        Harness harness;
        harness.newTab();
        const size_t firstResizes = harness.pty.handles[0]->resizes;
        const size_t secondResizes = harness.pty.handles[1]->resizes;

        harness.composer.resize(100, 40);

        STD_INSIST(harness.pty.handles[0]->resizes == firstResizes + 1);
        STD_INSIST(harness.pty.handles[1]->resizes == secondResizes + 1);
    }

    // A8: the terminal is handed a pane and its child is told the same
    // size, once. The shell pays a SIGWINCH per TIOCSWINSZ that changes
    // anything, so "reaches every handle" is not enough - it has to reach
    // each of them exactly once, and with the pane's two numbers the
    // right way round. 100 by 30 is neither square nor the 80 by 24 it
    // started at, so a transposed pair answers 30 columns and is caught.
    STD_TEST(EachResizeCostsEveryShellExactlyOneWinsize) {
        Harness harness;
        harness.newTab();
        const size_t firstResizes = harness.pty.handles[0]->resizes;
        const size_t secondResizes = harness.pty.handles[1]->resizes;

        harness.composer.resize(100, 30);

        STD_INSIST(harness.pty.handles[0]->resizes == firstResizes + 1);
        STD_INSIST(harness.pty.handles[1]->resizes == secondResizes + 1);
        for (size_t at = 0; at < 2; ++at) {
            STD_INSIST(harness.pty.handles[at]->size.columns == 100);
            STD_INSIST(harness.pty.handles[at]->size.rows == 30);
            // One pixel per cell in the harness, so the pixel pair is the
            // cell pair again - and it too has to keep its axes.
            STD_INSIST(harness.pty.handles[at]->size.pixelWidth == 100);
            STD_INSIST(harness.pty.handles[at]->size.pixelHeight == 30);
        }

        // A resize to the size the window already has costs the shell
        // nothing: no second ioctl, so no second SIGWINCH.
        const size_t settled = harness.pty.handles[0]->resizes;
        harness.composer.resize(100, 30);
        STD_INSIST(harness.pty.handles[0]->resizes == settled);
    }

    // A8: what "the pane that fills the window" is, asserted where it is
    // defined rather than only where its consequences show. windowPane()
    // is the single place outside Composer allowed to read the window's
    // grid and call the answer a pane, and both halves of that answer
    // matter: the grid is the window's, and the origin is zero because a
    // pane filling the window begins where the window's content does.
    //
    // The origin half had no assertion anywhere in unit_tests: an origin
    // invented here shifts every pointer report in the application, and
    // only the python mouse suite noticed. Splits will change this
    // function - T9/T10 - and this is the line that has to be rewritten
    // deliberately when they do.
    STD_TEST(ThePaneThatFillsTheWindowIsTheWindowsGridAtOriginZero) {
        Harness harness;
        harness.composer.resize(100, 30);

        const PaneGeometry pane = windowPane(harness.composer);

        STD_INSIST(pane.columns == harness.composer.columns);
        STD_INSIST(pane.rows == harness.composer.rows);
        // 100 by 30 is not square, so a transposed pair is caught here
        // and not left to a mapping downstream.
        STD_INSIST(pane.columns == 100);
        STD_INSIST(pane.rows == 30);
        STD_INSIST(pane.originX == 0);
        STD_INSIST(pane.originY == 0);
    }

    STD_TEST(ClosingReleasesAParkedClientWriteFiber) {
        Harness harness;
        harness.pty.blockNextWrite = true;
        harness.newTab();

        harness.sessions->activeTerminal()->sendBytes(StringView(u8"x"), true);
        STD_INSIST(harness.pty.writeEntered);
        STD_INSIST(!harness.pty.writeResumed);

        harness.closeTab();

        STD_INSIST(harness.pty.destroyed == 1);
        STD_INSIST(!harness.pty.writeResumed);
    }

    STD_TEST(SessionCountDoesNotLengthenTheInputChain) {
        Harness harness;
        harness.newTab();
        harness.newTab();
        size_t handlers = 0;
        for (IntrusiveNode* node = harness.composer.inputHandlers.mutFront(); node != harness.composer.inputHandlers.mutEnd(); node = node->next) {
            ++handlers;
        }

        // InputBindings and the one SessionSet handler.
        STD_INSIST(handlers == 2);
    }

    // A4/A5. The window is 80 x 24 with one pixel to a glyph, so a pane
    // rectangle and a pane grid are the same numbers here and a wrong
    // division shows up as a wrong column count on a real shell.
    STD_TEST(SplittingIsRefusedWhileTheOptionIsOff) {
        Harness harness;
        STD_INSIST(!harness.options.panes);

        STD_INSIST(!harness.sessions->splitFocused(SplitDirection::Vertical));
        STD_INSIST(!harness.sessions->splitFocused(SplitDirection::Horizontal));

        // Nothing was opened and nothing was moved: the negative control
        // for every test below, which all begin by turning the option on.
        Vector<SessionPane> panes;
        harness.sessions->visiblePanes(panes);
        STD_INSIST(panes.length() == 1);
        STD_INSIST(harness.pty.handles.length() == 1);
        STD_INSIST(SessionSet::liveSessions == 1);
    }

    STD_TEST(SplittingDividesTheTabAndNotTheWindowsTabs) {
        Harness harness;
        harness.options.panes = true;
        STD_INSIST(harness.sessions->splitFocused(SplitDirection::Vertical));

        // One tab still, two panes in it.
        STD_INSIST(harness.sessions->count() == 1);
        STD_INSIST(harness.sessions->activeIndex() == 0);
        Vector<SessionPane> panes;
        harness.sessions->visiblePanes(panes);
        STD_INSIST(panes.length() == 2);
        STD_INSIST(SessionSet::liveSessions == 2);

        // Side by side across the 80 pixel content box, and the new pane
        // has the focus.
        STD_INSIST(panes[0].area.x == 0);
        STD_INSIST(panes[0].area.width == 40);
        STD_INSIST(panes[1].area.x == 40);
        STD_INSIST(panes[1].area.width == 40);
        STD_INSIST(panes[0].area.height == 24);
        STD_INSIST(panes[1].area.height == 24);
        STD_INSIST(!panes[0].focused);
        STD_INSIST(panes[1].focused);
        STD_INSIST(harness.sessions->activeTerminal() == panes[1].terminal);
    }

    STD_TEST(EveryPaneGetsItsOwnGridAndTellsItsChild) {
        Harness harness;
        harness.options.panes = true;
        harness.sessions->splitFocused(SplitDirection::Vertical);
        harness.sessions->splitFocused(SplitDirection::Horizontal);

        // 80 x 24, divided vertically and then the right half
        // horizontally: 40 x 24, 40 x 12, 40 x 12.
        Vector<SessionPane> panes;
        harness.sessions->visiblePanes(panes);
        STD_INSIST(panes.length() == 3);
        STD_INSIST(harness.pty.handles.length() == 3);

        // A5-5: what the child is told is derived from the pane the
        // terminal was given, so the two cannot disagree. Counted off the
        // window instead, all three would read 80 x 24.
        const PtySize whole = harness.pty.handles[0]->size;
        const PtySize upper = harness.pty.handles[1]->size;
        const PtySize lower = harness.pty.handles[2]->size;
        STD_INSIST(whole.columns == 40 && whole.rows == 24);
        STD_INSIST(upper.columns == 40 && upper.rows == 12);
        STD_INSIST(lower.columns == 40 && lower.rows == 12);
        // And the pixel sizes follow the same grid rather than the
        // window's: one pixel to a glyph here.
        STD_INSIST(whole.pixelWidth == 40 && whole.pixelHeight == 24);
        STD_INSIST(lower.pixelWidth == 40 && lower.pixelHeight == 12);
    }

    STD_TEST(OnePaneIsGivenTheWholeContentBox) {
        Harness harness;

        Vector<SessionPane> panes;
        harness.sessions->visiblePanes(panes);
        STD_INSIST(panes.length() == 1);
        STD_INSIST(panes[0].area.x == 0);
        STD_INSIST(panes[0].area.y == 0);
        // The grid the composer published, to the cell: a window of one
        // pane behaves exactly as it did before there were panes, and
        // this is what says so.
        STD_INSIST(panes[0].area.width == harness.composer.columns * harness.composer.glyphWidth);
        STD_INSIST(panes[0].area.height == harness.composer.rows * harness.composer.glyphHeight);
        STD_INSIST(harness.pty.handles[0]->size.columns == harness.composer.columns);
        STD_INSIST(harness.pty.handles[0]->size.rows == harness.composer.rows);
    }

    STD_TEST(PanesDivideTheContentBoxAndNotTheWindow) {
        // A10: a border tells the window, the content box and a pane's
        // rectangle apart, and nothing else does. The content box is the
        // window less the chrome - here the whole 80 x 24, chrome being
        // nothing - while the grid inside it is 74 x 18, because the
        // pane's own border comes off the pane and not off the window.
        Harness harness{nullptr, 0, 3};
        harness.options.panes = true;
        STD_INSIST(harness.composer.chromeInsets().left == 0);
        STD_INSIST(harness.composer.paneInsets().left == 3);
        STD_INSIST(harness.composer.columns == 74);
        STD_INSIST(harness.composer.rows == 18);

        Vector<SessionPane> one;
        harness.sessions->visiblePanes(one);
        STD_INSIST(one.length() == 1);
        // The rectangle is the pane's outside, so it is the content box
        // whole; the grid inside it is still the one the composer
        // published, which is what says a window of one pane did not move
        // a pixel when the border changed hands.
        STD_INSIST(one[0].area.width == 80);
        STD_INSIST(one[0].area.height == 24);
        STD_INSIST(harness.pty.handles[0]->size.columns == 74);
        STD_INSIST(harness.pty.handles[0]->size.rows == 18);

        harness.sessions->splitFocused(SplitDirection::Vertical);
        Vector<SessionPane> two;
        harness.sessions->visiblePanes(two);
        STD_INSIST(two.length() == 2);
        // Half of the content box each, borders included: 40 and 40, not
        // 37 - the halves of a box the window's border had already been
        // taken out of.
        STD_INSIST(two[0].area.width == 40);
        STD_INSIST(two[1].area.width == 40);
        // And a border a side off each half, so 34 columns and not 40:
        // the border is charged to every pane, not once to the window.
        STD_INSIST(harness.pty.handles[0]->size.columns == 34);
        STD_INSIST(harness.pty.handles[1]->size.columns == 34);
        STD_INSIST(harness.pty.handles[0]->size.rows == 18);
        STD_INSIST(harness.pty.handles[1]->size.rows == 18);
        // The origin is the rectangle's and the content box's own, so the
        // first pane starts at zero and the second where the first ends.
        STD_INSIST(two[0].area.x == 0);
        STD_INSIST(two[1].area.x == 40);
    }

    // A10: the seam. Two panes side by side are two terminals, each in its
    // own border, so what separates their grids is border + whatever the
    // left pane could not fill with a whole cell + border - never the one
    // border a single-bordered window would have drawn between them.
    STD_TEST(TheSeamBetweenTwoPanesIsTwoBordersWide) {
        Harness harness{nullptr, 0, 3};
        harness.options.panes = true;
        harness.sessions->splitFocused(SplitDirection::Vertical);

        Vector<SessionPane> two;
        harness.sessions->visiblePanes(two);
        STD_INSIST(two.length() == 2);

        // Where each grid actually lands: the rectangle's edge plus the
        // pane's own inset, which is exactly what the backend adds when it
        // puts a grid inside a pane rectangle (render.h, surfacePane()).
        const int border = harness.composer.paneInsets().left;
        const int leftGridStart = two[0].area.x + border;
        const int leftGridEnd = leftGridStart + (int)(harness.pty.handles[0]->size.pixelWidth);
        const int rightGridStart = two[1].area.x + border;
        STD_INSIST(leftGridEnd == 37);
        STD_INSIST(rightGridStart == 43);
        // The columns come from paneGeometry and the origins from the
        // layout, so a pane that forgot its own border closes this gap
        // instead of merely narrowing it.
        STD_INSIST(rightGridStart - leftGridEnd == 2 * border);
    }

    // A8: the pane's origin is half of what SessionSet hands a terminal,
    // and it is the half nothing else can stand in for - the grid is
    // visible in the pty size, the origin only in where the terminal
    // thinks a pixel landed. An origin that never arrived maps a click in
    // the right-hand pane to a cell of the left-hand one, which looks
    // like a working terminal answering with the wrong cell.
    STD_TEST(EachPaneCountsPointerReportsFromItsOwnOrigin) {
        Harness harness;
        harness.options.panes = true;
        harness.sessions->splitFocused(SplitDirection::Vertical);
        Vector<SessionPane> panes;
        harness.sessions->visiblePanes(panes);
        STD_INSIST(panes.length() == 2);
        STD_INSIST(panes[1].area.x == 40);

        // SGR mouse reporting on both, so a report names its cell in
        // decimal rather than a byte that saturates.
        panes[0].terminal->feedPty(StringView(u8"\x1b[?1000h\x1b[?1006h"));
        panes[1].terminal->feedPty(StringView(u8"\x1b[?1000h\x1b[?1006h"));
        harness.pty.handles[0]->written.reset();
        harness.pty.handles[1]->written.reset();

        // One pixel to a glyph and no border, so window pixel 45 is the
        // 46th column of the window and the 6th of the right-hand pane.
        panes[1].terminal->pointerButton({plt::PointerButton::Primary, true, 45, 3, 0, 0.0});

        STD_INSIST(StringView(harness.pty.handles[1]->written).search(StringView(u8"\x1b[<0;6;4M")) != nullptr);
        // The column the window would have named had the origin not
        // arrived - the defect this catches.
        STD_INSIST(StringView(harness.pty.handles[1]->written).search(StringView(u8"\x1b[<0;46;4M")) == nullptr);
    }

    STD_TEST(ATabIsLabelledByThePaneTheUserIsTypingInto) {
        Harness harness;
        harness.options.panes = true;
        Vterm* const first = harness.sessions->activeTerminal();
        first->feedPty(StringView(u8"\x1b]0;left\x07"));
        STD_INSIST(harness.sessions->title(0).length() == 4);

        harness.sessions->splitFocused(SplitDirection::Vertical);
        harness.sessions->activeTerminal()->feedPty(StringView(u8"\x1b]0;right\x07"));

        // The focused pane's title, not the tab's first pane's.
        STD_INSIST(harness.sessions->title(0).length() == 5);
        harness.sessions->focusNeighbour(PaneSide::Left);
        STD_INSIST(harness.sessions->title(0).length() == 4);
    }

    STD_TEST(AClosedTabsTreeIsReusedAndNotAliased) {
        Harness harness;
        harness.newTab();
        Vterm* const second = harness.sessions->activeTerminal();
        STD_INSIST(harness.sessions->count() == 2);

        // The first tab goes; the tree it held is the one the next tab
        // takes. A tree left in two slots at once would be planted over
        // while it is still the surviving tab's.
        harness.sessions->activate(0);
        STD_INSIST(harness.sessions->close(0));
        STD_INSIST(harness.sessions->activeTerminal() == second);
        harness.newTab();
        Vterm* const third = harness.sessions->activeTerminal();
        STD_INSIST(harness.sessions->count() == 2);
        STD_INSIST(third != second);

        harness.sessions->activate(0);
        Vector<SessionPane> panes;
        harness.sessions->visiblePanes(panes);
        STD_INSIST(panes.length() == 1);
        STD_INSIST(panes[0].terminal == second);
    }

    STD_TEST(TheWindowsFocusReachesTheFocusedPaneAndOnlyIt) {
        Harness harness;
        harness.options.panes = true;
        harness.sessions->splitFocused(SplitDirection::Vertical);
        Vector<SessionPane> panes;
        harness.sessions->visiblePanes(panes);
        STD_INSIST(panes.length() == 2);

        // Both children ask to hear about focus (DECSET 1004); only the
        // focused pane's may be told.
        panes[0].terminal->feedPty(StringView(u8"\033[?1004h"));
        panes[1].terminal->feedPty(StringView(u8"\033[?1004h"));
        harness.pty.handles[0]->written.reset();
        harness.pty.handles[1]->written.reset();

        harness.windowFocus(false);
        harness.windowFocus(true);

        // CSI I is focus-in, CSI O focus-out.
        STD_INSIST(StringView(harness.pty.handles[1]->written).search(StringView(u8"\033[I")) != nullptr);
        STD_INSIST(StringView(harness.pty.handles[1]->written).search(StringView(u8"\033[O")) != nullptr);
        // A5: the neighbour is visible - it is in visiblePanes above and
        // renders - and it is not focused, so its child hears nothing.
        STD_INSIST(harness.pty.handles[0]->written.length() == 0);
    }

    STD_TEST(MovingTheFocusTellsBothPanesAndMovesNeitherOffScreen) {
        Harness harness;
        harness.options.panes = true;
        harness.sessions->splitFocused(SplitDirection::Vertical);
        Vector<SessionPane> panes;
        harness.sessions->visiblePanes(panes);
        panes[0].terminal->feedPty(StringView(u8"\033[?1004h"));
        panes[1].terminal->feedPty(StringView(u8"\033[?1004h"));
        harness.pty.handles[0]->written.reset();
        harness.pty.handles[1]->written.reset();

        STD_INSIST(harness.sessions->focusNeighbour(PaneSide::Left));

        // The pane taking the focus is told it has it; the pane losing it
        // is told it has lost it, which is the report a single "active"
        // state could not send while the pane stayed on screen.
        STD_INSIST(StringView(harness.pty.handles[0]->written).search(StringView(u8"\033[I")) != nullptr);
        STD_INSIST(StringView(harness.pty.handles[1]->written).search(StringView(u8"\033[O")) != nullptr);
        STD_INSIST(harness.sessions->activeTerminal() == panes[0].terminal);

        // Both are still visible, and their rectangles have not moved.
        Vector<SessionPane> after;
        harness.sessions->visiblePanes(after);
        STD_INSIST(after.length() == 2);
        STD_INSIST(after[0].focused);
        STD_INSIST(!after[1].focused);
        STD_INSIST(after[0].area.width == panes[0].area.width);
        STD_INSIST(after[1].area.x == panes[1].area.x);
    }

    STD_TEST(ABackgroundTabsPanesAreNeitherVisibleNorFocused) {
        Harness harness;
        harness.options.panes = true;
        harness.sessions->splitFocused(SplitDirection::Vertical);
        Vector<SessionPane> first;
        harness.sessions->visiblePanes(first);
        STD_INSIST(first.length() == 2);

        harness.newTab();
        STD_INSIST(harness.sessions->count() == 2);
        first[0].terminal->feedPty(StringView(u8"\033[?1004h"));
        first[1].terminal->feedPty(StringView(u8"\033[?1004h"));
        harness.pty.handles[0]->written.reset();
        harness.pty.handles[1]->written.reset();

        // The visible panes are the new tab's, and it holds one.
        Vector<SessionPane> second;
        harness.sessions->visiblePanes(second);
        STD_INSIST(second.length() == 1);
        STD_INSIST(second[0].terminal != first[0].terminal);
        STD_INSIST(second[0].terminal != first[1].terminal);
        STD_INSIST(second[0].focused);

        // And a window focus event reaches none of the backgrounded
        // panes: a background tab does not think it is focused.
        harness.windowFocus(false);
        harness.windowFocus(true);
        STD_INSIST(harness.pty.handles[0]->written.length() == 0);
        STD_INSIST(harness.pty.handles[1]->written.length() == 0);

        // Coming back restores both panes and the one that had the focus.
        harness.sessions->activate(0);
        Vector<SessionPane> back;
        harness.sessions->visiblePanes(back);
        STD_INSIST(back.length() == 2);
        STD_INSIST(back[1].terminal == first[1].terminal);
        STD_INSIST(back[1].focused);
        STD_INSIST(StringView(harness.pty.handles[1]->written).search(StringView(u8"\033[I")) != nullptr);
    }

    STD_TEST(ClosingAPaneGivesItsRoomToTheSurvivorAndKeepsTheTab) {
        Harness harness;
        harness.options.panes = true;
        harness.sessions->splitFocused(SplitDirection::Vertical);
        STD_INSIST(harness.pty.handles[0]->size.columns == 40);

        STD_INSIST(harness.sessions->closeFocusedPane());

        STD_INSIST(harness.sessions->count() == 1);
        STD_INSIST(SessionSet::liveSessions == 1);
        Vector<SessionPane> panes;
        harness.sessions->visiblePanes(panes);
        STD_INSIST(panes.length() == 1);
        STD_INSIST(panes[0].area.width == 80);
        STD_INSIST(panes[0].focused);
        // The survivor's child hears its new size, not just the layout.
        STD_INSIST(harness.pty.handles[0]->size.columns == 80);
        STD_INSIST(harness.sessions->activeTerminal() == panes[0].terminal);
    }

    // T10 acceptance: the chord divides, and both panes are live and
    // take input independently. The second half is the one that is easy
    // to fake: a split that opened a second shell but left every
    // keystroke going to the first would satisfy every count below.
    STD_TEST(TheSplitChordsDivideAndBothPanesTakeInputOfTheirOwn) {
        Harness harness;
        harness.options.panes = true;

        harness.splitVertical();
        Vector<SessionPane> panes;
        harness.sessions->visiblePanes(panes);
        STD_INSIST(panes.length() == 2);
        // Side by side: same top, different left.
        STD_INSIST(panes[0].area.y == panes[1].area.y);
        STD_INSIST(panes[1].area.x == 40);
        // The new pane has the focus, which is where the next keystroke
        // must go.
        STD_INSIST(panes[1].focused);

        harness.pty.handles[0]->written.reset();
        harness.pty.handles[1]->written.reset();
        harness.keyPress(plt::InputKey::Enter);
        STD_INSIST(StringView(harness.pty.handles[1]->written).search(StringView(u8"\r")) != nullptr);
        STD_INSIST(harness.pty.handles[0]->written.length() == 0);

        // The focus moves back by hand, and the bytes move with it: the
        // pane that was quiet a moment ago is a live terminal, not a
        // picture of one.
        harness.sessions->focusPane(panes[0].id);
        harness.pty.handles[1]->written.reset();
        harness.keyPress(plt::InputKey::Enter);
        STD_INSIST(StringView(harness.pty.handles[0]->written).search(StringView(u8"\r")) != nullptr);
        STD_INSIST(harness.pty.handles[1]->written.length() == 0);

        // The other axis stacks instead: same left, different top.
        harness.splitHorizontal();
        Vector<SessionPane> stacked;
        harness.sessions->visiblePanes(stacked);
        STD_INSIST(stacked.length() == 3);
        STD_INSIST(stacked[0].area.x == stacked[1].area.x);
        STD_INSIST(stacked[1].area.y == 12);
    }

    // The inner of the two locks. The chord itself is gated in the
    // binding table (input_bindings_ut), and this is the door behind it:
    // even published straight into the listener list, the action does
    // nothing while the option is off.
    STD_TEST(TheSplitChordsDoNothingWhileThePanesOptionIsOff) {
        Harness harness;
        STD_INSIST(!harness.options.panes);

        harness.splitVertical();
        harness.splitHorizontal();

        Vector<SessionPane> panes;
        harness.sessions->visiblePanes(panes);
        STD_INSIST(panes.length() == 1);
        STD_INSIST(SessionSet::liveSessions == 1);
    }

    // cmd+w with panes on means "close what I am looking at". The tab
    // goes only when the pane was the tab's last one, which is the same
    // answer the chord always gave for a window of single-pane tabs.
    STD_TEST(TheCloseChordTakesThePaneAndOnlyThenTheTab) {
        Harness harness;
        harness.options.panes = true;
        harness.newTab();
        harness.splitVertical();
        STD_INSIST(harness.sessions->count() == 2);
        Vector<SessionPane> panes;
        harness.sessions->visiblePanes(panes);
        STD_INSIST(panes.length() == 2);

        harness.closeTab();
        // The tab survives with one pane, which now has the room.
        STD_INSIST(harness.sessions->count() == 2);
        Vector<SessionPane> left;
        harness.sessions->visiblePanes(left);
        STD_INSIST(left.length() == 1);
        STD_INSIST(left[0].area.width == 80);
        STD_INSIST(harness.pty.handles[1]->size.columns == 80);

        // Now it is the tab's last pane, and the chord takes the tab.
        harness.closeTab();
        STD_INSIST(harness.sessions->count() == 1);
    }

    STD_TEST(ClosingTheLastPaneOfATabClosesTheTab) {
        Harness harness;
        harness.options.panes = true;
        harness.newTab();
        harness.sessions->splitFocused(SplitDirection::Vertical);
        STD_INSIST(harness.sessions->count() == 2);

        STD_INSIST(harness.sessions->closeFocusedPane());
        STD_INSIST(harness.sessions->count() == 2);
        // Now the second tab holds one pane; closing it takes the tab.
        STD_INSIST(harness.sessions->closeFocusedPane());
        STD_INSIST(harness.sessions->count() == 1);
        STD_INSIST(harness.sessions->activeIndex() == 0);
        // And the last pane of the last tab says so, which is how the
        // window learns to close.
        STD_INSIST(!harness.sessions->closeFocusedPane());
        STD_INSIST(harness.sessions->count() == 0);
    }

    STD_TEST(ClosingATabTakesEveryPaneInIt) {
        Harness harness;
        harness.options.panes = true;
        harness.sessions->splitFocused(SplitDirection::Vertical);
        harness.sessions->splitFocused(SplitDirection::Horizontal);
        harness.newTab();
        STD_INSIST(SessionSet::liveSessions == 4);

        // The three-pane tab goes whole.
        STD_INSIST(harness.sessions->close(0));
        STD_INSIST(harness.sessions->count() == 1);
        STD_INSIST(SessionSet::liveSessions == 1);
        Vector<SessionPane> panes;
        harness.sessions->visiblePanes(panes);
        STD_INSIST(panes.length() == 1);
        STD_INSIST(panes[0].area.width == 80);
    }

    STD_TEST(WindowResizeReachesEveryPaneOfEveryTab) {
        Harness harness;
        harness.options.panes = true;
        harness.sessions->splitFocused(SplitDirection::Vertical);
        harness.newTab();
        harness.sessions->splitFocused(SplitDirection::Horizontal);

        harness.composer.resize(120, 40);
        publish(harness.composer.resizedListeners);

        // Background tab: two panes side by side across 120.
        STD_INSIST(harness.pty.handles[0]->size.columns == 60);
        STD_INSIST(harness.pty.handles[1]->size.columns == 60);
        STD_INSIST(harness.pty.handles[0]->size.rows == 40);
        // Active tab: two panes stacked down 40.
        STD_INSIST(harness.pty.handles[2]->size.columns == 120);
        STD_INSIST(harness.pty.handles[3]->size.columns == 120);
        STD_INSIST(harness.pty.handles[2]->size.rows == 20);
        STD_INSIST(harness.pty.handles[3]->size.rows == 20);
    }

    // F7-1 (oracle audit). applyLayout() does two things to every pane -
    // hands the terminal its new grid and hands the shell its new winsize
    // - and the test above reads only the second. Delete the
    // paneResized() line and the resize path goes silent towards every
    // terminal in the window while every assertion above still holds. The
    // one test that objected was SumsTheExtraStoreBudgetOverEveryLivePane,
    // which reaches applyLayout() through a split and never through a
    // resize, and objected about a cell budget rather than about a
    // terminal being told anything.
    //
    // Mode 2048 is the terminal's own voice: the CSI 48 report comes out
    // of resizeGrid(), and on this path only paneResized() reaches it. A
    // shell told correctly by a terminal that was never told is exactly
    // the state wave 8 would have drawn.
    STD_TEST(TheResizeTellsEveryPaneTerminalAndNotOnlyItsShell) {
        Harness harness;
        harness.options.panes = true;
        harness.sessions->splitFocused(SplitDirection::Vertical);

        Vector<SessionPane> panes;
        harness.sessions->visiblePanes(panes);
        STD_INSIST(panes.length() == 2);
        panes[0].terminal->feedPty(StringView(u8"\x1b[?2048h"));
        panes[1].terminal->feedPty(StringView(u8"\x1b[?2048h"));
        // Enabling the mode reports once by itself, so what is left after
        // the reset is the resize's own doing and nothing else.
        harness.pty.handles[0]->written.reset();
        harness.pty.handles[1]->written.reset();

        harness.composer.resize(120, 40);
        publish(harness.composer.resizedListeners);

        // CSI 48 ; rows ; columns ; pixel height ; pixel width t, at one
        // pixel to a glyph: each terminal names its own half of 120.
        STD_INSIST(StringView(harness.pty.handles[0]->written).search(StringView(u8"\x1b[48;40;60;40;60t")) != nullptr);
        STD_INSIST(StringView(harness.pty.handles[1]->written).search(StringView(u8"\x1b[48;40;60;40;60t")) != nullptr);
        // And never the window's own grid, which is what a terminal still
        // holding the whole surface would have answered with.
        STD_INSIST(StringView(harness.pty.handles[0]->written).search(StringView(u8"\x1b[48;40;120;40;120t")) == nullptr);
        STD_INSIST(StringView(harness.pty.handles[1]->written).search(StringView(u8"\x1b[48;40;120;40;120t")) == nullptr);
    }

    // F7-2 (oracle audit). Every other pane test runs on a square glyph of
    // one pixel, where columns * glyphWidth is the column count itself and
    // pairing an axis with the other axis's glyph changes no number
    // anywhere. On a real 8 x 16 glyph that same swap is a wrong winsize
    // under every shell in the window, and a full-screen program draws to
    // it. Two by three pixels and a border of one, so that all four
    // numbers a mistake could reach for stay distinct.
    STD_TEST(ThePixelSizeEachShellIsToldPairsEachAxisWithItsOwnGlyph) {
        Harness harness{nullptr, 0, 1, 2, 3};
        harness.options.panes = true;
        STD_INSIST(harness.composer.glyphWidth == 2);
        STD_INSIST(harness.composer.glyphHeight == 3);

        // One pane: 80 x 24 less a border a side is 78 x 22, which is 39
        // columns of two pixels and 7 rows of three.
        const PtySize whole = harness.pty.handles[0]->size;
        STD_INSIST(whole.columns == 39);
        STD_INSIST(whole.rows == 7);
        STD_INSIST(whole.pixelWidth == 78);
        STD_INSIST(whole.pixelHeight == 21);

        // And the same across a split, where the two axes are divided
        // unequally: 40 wide less two borders is 38, or 19 columns, while
        // the rows are untouched.
        harness.sessions->splitFocused(SplitDirection::Vertical);
        const PtySize left = harness.pty.handles[0]->size;
        const PtySize right = harness.pty.handles[1]->size;
        STD_INSIST(left.columns == 19 && right.columns == 19);
        STD_INSIST(left.rows == 7 && right.rows == 7);
        STD_INSIST(left.pixelWidth == 38 && right.pixelWidth == 38);
        STD_INSIST(left.pixelHeight == 21 && right.pixelHeight == 21);
    }

    // Q3. A shell that exits inside a split takes its pane and leaves
    // the tab standing. Nothing checked this: T9 declared the gap and
    // the wave-7 acceptance pass reproduced it - the mutation that makes
    // closeEndedSessions() close the *tab* instead of the pane
    // (closePane(pane) -> close(tabOf(pane))) passed the whole suite.
    //
    // It could not be reached from here before because the EOF path is
    // private and runs on the loop: PtyReadBody turns a null acquire()
    // into ptyEof(), which queues the pane and rings a LoopWake, and the
    // poller then calls closeEndedSessions(). So the stand has to do
    // what the application does - report the child gone and pump the
    // loop - rather than call anything directly.
    //
    // And it needs *two panes in one tab*. pty_ut.cpp already drives a
    // real shell to EOF, but with one pane per tab, where closing the
    // pane and closing the tab are the same outcome and the mutation is
    // invisible.
    STD_TEST(AShellThatExitsTakesItsPaneAndLeavesTheTabStanding) {
        Harness harness;
        harness.options.panes = true;
        STD_INSIST(harness.sessions->splitFocused(SplitDirection::Vertical));
        Vector<SessionPane> before;
        harness.sessions->visiblePanes(before);
        STD_INSIST(before.length() == 2);
        STD_INSIST(harness.sessions->count() == 1);
        STD_INSIST(SessionSet::liveSessions == 2);
        Vterm* const survivor = before[0].terminal;
        Vterm* const doomed = before[1].terminal;
        STD_INSIST(harness.pty.handles.length() == 2);

        // The second pane's child exits. Everything after this is the
        // application's own path, driven by the same loop it runs on.
        harness.pty.handles[1]->reportEof();
        auto* const poller = static_cast<plt::PollerLoop*>(harness.composer.platform->poller());
        Timeout closeTimeout;
        poller->timeout(testTimeoutUs, closeTimeout);
        // Pumped until the count moves at all, not until it reaches the
        // right number: a stand that waited for the right number would
        // report a wrong one as a five-second timeout, and the failure
        // has to name the defect rather than the wait.
        while (SessionSet::liveSessions == 2 && !closeTimeout.fired) {
            poller->dispatchTimers();
            if (SessionSet::liveSessions == 2 && !closeTimeout.fired) {
                poller->wait(poller->nextDeadline());
            }
        }
        poller->cancel(closeTimeout);
        STD_INSIST(!closeTimeout.fired);

        // The tab is still here, holding the pane whose shell is alive,
        // and it has been given the room the dead one left. A frame that
        // closed the tab instead would answer zero tabs here.
        STD_INSIST(harness.sessions->count() == 1);
        STD_INSIST(SessionSet::liveSessions == 1);
        Vector<SessionPane> after;
        harness.sessions->visiblePanes(after);
        STD_INSIST(after.length() == 1);
        STD_INSIST(after[0].terminal == survivor);
        STD_INSIST(after[0].terminal != doomed);
        STD_INSIST(after[0].focused);
        STD_INSIST(after[0].area.width == 80);
        // And the survivor's child was told its new size, not just the
        // layout: the pane that lost a neighbour is a resize like any
        // other.
        STD_INSIST(harness.pty.handles[0]->size.columns == 80);
        STD_INSIST(harness.sessions->activeTerminal() == survivor);
    }

    // A11. The store is one per window; its budget has to be the sum
    // over the live panes, not the last writer's own count and not an
    // upper bound taken off the window. slotBudget() is ten per cell and
    // is the only number the store publishes.
    STD_TEST(SumsTheExtraStoreBudgetOverEveryLivePane) {
        // A scrollback is what makes the sum differ from the window: it
        // is charged per pane, so two half-height panes hold more cells
        // between them than the one pane they replaced. Without it every
        // division of a grid holds exactly the cells it divided, and a
        // budget taken off the window would look identical to a sum.
        constexpr size_t saveLines = 100;
        Harness harness{nullptr, saveLines};
        harness.options.panes = true;
        const size_t whole = (size_t)(80) * (24 + saveLines);
        STD_INSIST(harness.composer.cellExtras->slotBudget() == whole * 10);

        harness.sessions->splitFocused(SplitDirection::Horizontal);
        const size_t half = (size_t)(80) * (12 + saveLines);
        STD_INSIST(harness.composer.cellExtras->slotBudget() == 2 * half * 10);
        // The three numbers a wrong rule would have answered with, each
        // different from the sum: the last writer's own count, the pane
        // before it, and the window-sized floor F5 could only reach for.
        STD_INSIST(2 * half > whole);
        STD_INSIST(half * 10 != 2 * half * 10);
        STD_INSIST(whole * 10 != 2 * half * 10);

        // A background tab counts too: its shell keeps parsing into the
        // same store while nobody is looking at it.
        harness.newTab();
        STD_INSIST(harness.composer.cellExtras->slotBudget() == (2 * half + whole) * 10);

        // And a pane that goes takes its share of the budget with it.
        STD_INSIST(harness.sessions->closeFocusedPane());
        STD_INSIST(harness.composer.cellExtras->slotBudget() == 2 * half * 10);
    }
}
