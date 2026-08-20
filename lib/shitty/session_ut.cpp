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

        Chunk* acquire() override {
            for (;;) {
                composer.platform->scheduler()->current()->park();
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
        explicit Harness(size_t* destroyed = nullptr, u16 saveLines = 0, u16 border = 0)
            : composer(*pool->make<Composer>(pool.mutPtr()))
            , pty(composer, destroyed == nullptr ? ownedDestroyed : *destroyed)
        {
            options.saveLines = saveLines;
            options.border = border;
            composer.platform = plt::createHeadlessPlatform(*composer.pool);
            composer.window = composer.platform->createWindow(*composer.pool, {.width = 80, .height = 24});
            composer.setGlyphSize(1, 1);
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
        // A1/A10: a border makes the content box smaller than the window,
        // which is the only configuration that can tell them apart. 80 x
        // 24 pixels less three a side is 74 x 18.
        Harness harness{nullptr, 0, 3};
        harness.options.panes = true;
        STD_INSIST(harness.composer.contentInsets().left == 3);
        STD_INSIST(harness.composer.columns == 74);
        STD_INSIST(harness.composer.rows == 18);

        Vector<SessionPane> one;
        harness.sessions->visiblePanes(one);
        STD_INSIST(one.length() == 1);
        STD_INSIST(one[0].area.width == 74);
        STD_INSIST(one[0].area.height == 18);
        STD_INSIST(harness.pty.handles[0]->size.columns == 74);

        harness.sessions->splitFocused(SplitDirection::Vertical);
        Vector<SessionPane> two;
        harness.sessions->visiblePanes(two);
        STD_INSIST(two.length() == 2);
        // Half of the content box, not half of the window (40) and not
        // the content box with the window's insets taken out twice (68,
        // halving to 34).
        STD_INSIST(two[0].area.width == 37);
        STD_INSIST(two[1].area.width == 37);
        STD_INSIST(harness.pty.handles[0]->size.columns == 37);
        STD_INSIST(harness.pty.handles[1]->size.columns == 37);
        // The origin is inside the content box, so the first pane starts
        // at zero and not at the border.
        STD_INSIST(two[0].area.x == 0);
        STD_INSIST(two[1].area.x == 37);
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
