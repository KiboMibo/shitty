/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "application.h"

#include "composer.h"
#include "grid_geometry.h"
#include "listener.h"
#include "options.h"
#include "quick_frame_store.h"
#include "pane_layout.h"
#include "session.h"
#include "ui_quick_hotkey.h"

#include <plt/input.h>
#include <plt/platform.h>
#include <plt/platform_headless.h>
#include <plt/poller.h>
#include <plt/poller_loop.h>
#include <plt/window.h>

#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/tst/ut.h>

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

using namespace stl;

namespace {
    // Mirrors quick_frame_store_ut.cpp's makeTempDir(): a mkdtemp()
    // directory this process actually owns, so defaultQuickFramePath()
    // has a real, writable directory to place the state file next to.
    void makeTempDir(StringBuilder& dir) {
        const char* const directory = getenv("TMPDIR");
        dir << StringView(directory != nullptr ? directory : "/tmp") << StringView(u8"/application_ut.XXXXXX");
        STD_INSIST(mkdtemp(dir.cStr()) != nullptr);
    }
}

namespace {
    // R8-test. A forked /bin/sh inherits MallocScribble from this process,
    // and libmalloc then prints its "enabling scribbling to detect mods to
    // free blocks" banner to the child's stderr - which is the slave side
    // of this pane's pty. The line arrives as CR/LF terminated text, lands
    // in the pane's grid, and moves the cursor off the home cell; the
    // anchor assertions below then read a row that the shell never wrote.
    //
    // Measured with a probe that fed one banner-shaped line to the focused
    // pane before the first frame: count and x hold, y comes back 56 where
    // 18 is expected - two whole glyph heights - which is exactly the
    // shape of the flake seen once in a full run under MallocScribble
    // (drive.anchor.y, application_ut.cpp:332 at e74011b8) and could not
    // be reproduced by running the test on its own.
    //
    // Unset rather than worked around: libmalloc reads the variable when
    // this process starts, so clearing it now leaves this process's own
    // scribbling on and only stops children inheriting it - and no child
    // of these tests is under test. Only sound while tests run serially,
    // which is what build.py asks for (--threads=1).
    void keepMallocDebugOutOfTheChildren() {
        unsetenv("MallocScribble");
        unsetenv("MallocPreScribble");
        unsetenv("MallocGuardEdges");
    }

    struct SavedSignals {
        SavedSignals() {
            STD_INSIST(sigaction(SIGCHLD, nullptr, &child) == 0);
            STD_INSIST(sigaction(SIGINT, nullptr, &interrupt) == 0);
            STD_INSIST(sigaction(SIGQUIT, nullptr, &quit) == 0);
        }

        ~SavedSignals() noexcept {
            sigaction(SIGCHLD, &child, nullptr);
            sigaction(SIGINT, &interrupt, nullptr);
            sigaction(SIGQUIT, &quit, nullptr);
        }

        struct sigaction child{};
        struct sigaction interrupt{};
        struct sigaction quit{};
    };

    struct DriveApplication final: plt::TimerCallback {
        explicit DriveApplication(Composer& composer_)
            : composer(composer_)
        {
        }

        void ready() override {
            fired = true;
            auto& window = static_cast<plt::WindowHeadless&>(*composer.window);
            // F4, I3: two reserves that are not each other, set before
            // the frame that anchors the input method. Without them the
            // horizontal inset and the vertical one are both the border
            // and the anchor can take either for either - which is how
            // R3-test's R16 (the IME rect reading its x out of
            // insets.top) stayed alive through two waves. The sides are
            // the ones real chrome uses, and the numbers are small
            // enough to leave the 20x4 grid a grid.
            composer.setChromeReserve(ChromeSide::Left, 3);
            composer.setChromeReserve(ChromeSide::Top, 16);
            if (splitPanes) {
                // T10/T13: two live panes before the first frame, so what
                // is dispatched below is the multi-pane frame. The focus
                // then goes back to the *first* pane, which is what lets
                // the anchor tell "the focused pane" from "the last pane
                // in the list" - left where the split put it, the two are
                // one pane and the assertion asserts nothing.
                split = composer.sessions->splitFocused(SplitDirection::Vertical);
                Vector<SessionPane> panes;
                composer.sessions->visiblePanes(panes);
                if (panes.length() == 2) {
                    composer.sessions->focusPane(panes[0].id);
                    secondPane = panes[1].id;
                    firstTerminal = panes[0].terminal;
                }
            }
            framePresented = window.dispatchFrame();
            // One present for the whole tab, not one per pane.
            generationAfterFirstFrame = window.presentedFrame().generation;
            // Read here and not after run(): later frames anchor on
            // whichever pane has the focus then, and what is under test is
            // the frame just dispatched.
            anchor = window.requestedTextInputRect();

            // Enter one line through the same platform-facing sink used by
            // Wayland/Cocoa. This crosses FiberInputSink, InputRouter,
            // SessionSet and Vterm before reaching the real PTY handle.
            composer.input->text({.codepoint = 'g'});
            composer.input->text({.codepoint = 'o'});
            composer.input->key({
                .key = plt::InputKey::Enter,
                .action = plt::InputAction::Press,
            });
            composer.input->flush();
            if (secondPane != 0) {
                // A second multi-pane frame, so the pane walk is not only
                // ever seen on the first one. The frame has to be
                // *requested* or dispatchFrame() has nothing to dispatch -
                // it was not, at first, and this whole half of the stand
                // ran as a no-op while looking like a test.
                //
                // What it does NOT reach is the retained-output branch:
                // both panes arrive at the frame with output pending every
                // time, and draining the second one by hand immediately
                // before the request does not change that - it is silent
                // when drained and speaking again inside the frame, for a
                // reason left unfound. So the two mutations that would
                // prove that branch - consume() on every pane, and
                // dropping the retained pane - pass in silence, and no
                // coverage of it should be read into this test. Written
                // down in docs/reports/T10-splits-2026-08-20.md for
                // R8-test rather than left as a green tick.
                firstTerminal->feedPty(StringView(u8"x"));
                composer.window->requestFrame();
                secondFramePresented = window.dispatchFrame();
                // And the other shell gets its line too, or run() never
                // returns: the loop ends when the last session does.
                composer.sessions->focusPane(secondPane);
                if (quietPaneProbe) {
                    driveQuietPaneFrame(window);
                }
                composer.input->text({.codepoint = 'g'});
                composer.input->text({.codepoint = 'o'});
                composer.input->key({
                    .key = plt::InputKey::Enter,
                    .action = plt::InputAction::Press,
                });
                composer.input->flush();
            }
        }

        // R8-test, T10's open hole: a frame in which one pane speaks and
        // the other hands over its retained form.
        //
        // Why this cannot be done on the frames above: while the window's
        // geometry is still settling, ApplicationImpl::frame() itself arms
        // every pane before presentTerminal() gets to ask any of them -
        // updateWindowInfo() -> Composer::resize() -> CallSessionsResize
        // -> SessionSet::applyLayout() -> Vterm::paneResized(), whose
        // cf->expose() plus redraw() reaches every pane of the tab and not
        // only the ones whose grid moved (98a08f42, and vterm.cpp's
        // paneResized). The first frame runs fontChanged() for the initial
        // geometry, so the second frame arrives with genuinely new numbers
        // and resize() fans out again. From the third frame on the numbers
        // repeat, resize() returns early, and a pane that was not written
        // to stays quiet.
        void driveQuietPaneFrame(plt::WindowHeadless& window) {
            // Two frames to let the geometry stop moving. The drain below
            // is what makes the result not depend on this count being
            // exactly right - but the count has to be enough for resize()
            // to have gone quiet, or the drain is undone inside the frame.
            composer.window->requestFrame();
            window.dispatchFrame();
            composer.window->requestFrame();
            window.dispatchFrame();

            Vector<SessionPane> panes;
            composer.sessions->visiblePanes(panes);
            if (panes.length() != 2) {
                return;
            }
            // The focused pane is the one that will say nothing: whatever
            // it still held is taken here, so it reaches presentTerminal()
            // with no output() at all.
            quietPaneOriginX = panes[1].area.x;
            quietPaneOriginY = panes[1].area.y;
            Vterm* const quiet = panes[1].terminal;
            if (quiet->output() != nullptr) {
                quiet->consume();
            }
            quietPaneDrained = quiet->output() == nullptr;

            // ...and the other one does speak, so what follows is a frame
            // rather than the repaint a window where nobody spoke gets.
            anchorsBeforeQuietFrame = window.requestedTextInputRect().count;
            firstTerminal->feedPty(StringView(u8"y"));
            composer.window->requestFrame();
            quietFramePresented = window.dispatchFrame();
            quietAnchor = window.requestedTextInputRect();
        }

        Composer& composer;
        bool fired = false;
        bool framePresented = false;
        bool splitPanes = false;
        bool split = false;
        u64 secondPane = 0;
        Vterm* firstTerminal = nullptr;
        bool secondFramePresented = false;
        u64 generationAfterFirstFrame = 0;
        plt::WindowTextInputRect anchor;

        // R8-test: the retained-output half of the frame. Off for T10's
        // own stand, which asserts about the frames before this one.
        bool quietPaneProbe = false;
        bool quietPaneDrained = false;
        bool quietFramePresented = false;
        u64 anchorsBeforeQuietFrame = 0;
        i32 quietPaneOriginX = 0;
        i32 quietPaneOriginY = 0;
        plt::WindowTextInputRect quietAnchor;
    };

    struct StopOnTimeout final: plt::TimerCallback {
        explicit StopOnTimeout(plt::Platform& platform_)
            : platform(platform_)
        {
        }

        void ready() override {
            fired = true;
            platform.stop();
        }

        plt::Platform& platform;
        bool fired = false;
    };
}

STD_TEST_SUITE(ApplicationProduction) {
    STD_TEST(HeadlessRunWiresPresentsAndTearsDownProductionComponents) {
        SavedSignals savedSignals;
        keepMallocDebugOutOfTheChildren();
        // Application and its threaded Pty have the same process lifetime
        // here as in runMain. Sessions still tear down through their arenas.
        ObjPool* const pool = ObjPool::fromMemoryRaw();
        Composer& composer = *pool->make<Composer>(pool);
        plt::InputSink* const router = composer.input;
        plt::Platform* const platform = plt::createHeadlessPlatform(*pool);
        composer.platform = platform;
        auto* const poller = static_cast<plt::PollerLoop*>(platform->poller());
        DriveApplication drive(composer);
        StopOnTimeout timeout(*platform);
        poller->timeout(1, drive);
        poller->timeout(5'000'000, timeout);

        Application* const application = Application::create(composer);
        char program[] = "application_ut";
        char config[] = "-config";
        char configPath[] = "/dev/null";
        char geometry[] = "-geometry";
        char geometryValue[] = "20x4";
        char execute[] = "-e";
        char shell[] = "/bin/sh";
        char commandFlag[] = "-c";
        char script[] = "IFS= read -r line; printf '\\033]2;orchestrated\\007seen:%s\\n' \"$line\"";
        char* argv[] = {
            program,
            config,
            configPath,
            geometry,
            geometryValue,
            execute,
            shell,
            commandFlag,
            script,
            nullptr,
        };

        const int result = application->run(9, argv);
        poller->cancel(timeout);

        STD_INSIST(result == 0);
        STD_INSIST(drive.fired);
        STD_INSIST(drive.framePresented);
        STD_INSIST(!timeout.fired);
        STD_INSIST(composer.platform == platform);
        STD_INSIST(composer.input != router);
        STD_INSIST(composer.window != nullptr);
        STD_INSIST(composer.pty != nullptr);
        STD_INSIST(composer.sessions != nullptr);
        STD_INSIST(composer.renderer == nullptr);
        STD_INSIST(SessionSet::liveSessions == 0);

        auto& window = static_cast<plt::WindowHeadless&>(*composer.window);
        STD_INSIST(window.presentedFrame().generation == 1);
        STD_INSIST(window.title() == StringView(u8"orchestrated"));

        // The input method's candidate window is anchored to the cursor
        // cell, and the cell's origin takes its x from the horizontal
        // inset and its y from the vertical one. The frame above is
        // presented before a single keystroke, so the cursor is the home
        // cell and the anchor is the content box's own corner - the two
        // reserves DriveApplication set make that corner two different
        // numbers, which is the whole point of asserting it.
        const plt::WindowTextInputRect anchor = window.requestedTextInputRect();
        const Insets insets = composer.contentInsets();

        STD_INSIST(anchor.count == 1);
        STD_INSIST(insets.left != insets.top);
        STD_INSIST(anchor.x == (i32)(insets.left));
        STD_INSIST(anchor.y == (i32)(insets.top));
        STD_INSIST(anchor.width == composer.glyphWidth);
        STD_INSIST(anchor.height == composer.glyphHeight);

        // R8-test: no reaping here, and deliberately none.
        //
        // This used to be `while (waitpid(-1, nullptr, 0) > 0) {}`, written
        // when the Pty suite waited on -1 as well and could pick up this
        // suite's zombies instead of its own. That end has since been
        // fixed - pty_ut.cpp reaps by pid and says why, and
        // PtyHandle::childPid() exists for exactly that - so the reason for
        // draining here is gone, while the drain itself became the same
        // bug pointing the other way: a blind wait takes whichever corpse
        // is ready, including a child another suite is about to wait for.
        //
        // Measured, at e74011b8 plus this branch, with
        // `--threads=8 Pty Fiber ApplicationProduction ToggleQuickWindow`:
        //
        //   with the drains        5 of 6 runs red, always
        //                          `waitpid(child, &status, 0) == child`
        //                          at pty_ut.cpp:226
        //   without the drains     0 of 13 runs with that failure
        //   without this suite     0 of 6 runs red, drains or no drains
        //
        // The drains were the thief, and the second row is what says so:
        // the only thing that changed between the first two is these three
        // loops.
        //
        // What --threads=8 does NOT become is green: it has other,
        // structural cross-suite couplings that are not reaping and are not
        // a test file's to fix. SessionSet::liveSessions is one static for
        // the whole process, so Pty::EofClosesOneSessionBeforeItsFollowupWake
        // counts this suite's sessions too (one run in the thirteen), and
        // three Application::run() at once deadlock on the same static and
        // on the process-wide SIGCHLD disposition (one run in the thirteen).
        // The build runs this binary at --threads=1 (build.py), which is
        // where none of that arises.
        //
        // What is left behind is a handful of zombies for the lifetime of
        // the test binary. Nothing waits on -1 any more, so nothing can
        // trip over them. Reaping them properly would mean waiting on the
        // pids of *these* shells, and no seam hands them over: run() makes
        // the Pty itself (application.cpp) and neither SessionSet nor Vterm
        // exposes the handle whose childPid() would answer.
    }

    // T10/T13 acceptance: the frame is every visible pane, and the whole
    // production path is walked - Application::run, the renderer, the
    // window - rather than a harness standing in for it. Until this, the
    // multi-pane presentTerminal() had no stand at all: every renderer
    // test hands the backend a frame it built itself, and every session
    // test stops at the pane list.
    STD_TEST(ATabOfTwoPanesPresentsOneFrameAndAnchorsOnTheFocusedOne) {
        SavedSignals savedSignals;
        keepMallocDebugOutOfTheChildren();
        ObjPool* const pool = ObjPool::fromMemoryRaw();
        Composer& composer = *pool->make<Composer>(pool);
        plt::Platform* const platform = plt::createHeadlessPlatform(*pool);
        composer.platform = platform;
        auto* const poller = static_cast<plt::PollerLoop*>(platform->poller());
        DriveApplication drive(composer);
        drive.splitPanes = true;
        StopOnTimeout timeout(*platform);
        poller->timeout(1, drive);
        poller->timeout(5'000'000, timeout);

        Application* const application = Application::create(composer);
        char program[] = "application_ut";
        char config[] = "-config";
        char configPath[] = "/dev/null";
        char panes[] = "-panes";
        char geometry[] = "-geometry";
        char geometryValue[] = "20x4";
        char execute[] = "-e";
        char shell[] = "/bin/sh";
        char commandFlag[] = "-c";
        char script[] = "IFS= read -r line; printf 'seen:%s\\n' \"$line\"";
        char* argv[] = {
            program,
            config,
            configPath,
            panes,
            geometry,
            geometryValue,
            execute,
            shell,
            commandFlag,
            script,
            nullptr,
        };

        const int result = application->run(10, argv);
        poller->cancel(timeout);

        STD_INSIST(result == 0);
        STD_INSIST(drive.split);
        STD_INSIST(drive.secondPane != 0);
        STD_INSIST(drive.framePresented);
        STD_INSIST(drive.secondFramePresented);
        STD_INSIST(!timeout.fired);

        // One present for the whole tab, not one per pane.
        STD_INSIST(drive.generationAfterFirstFrame == 1);

        // The candidate window follows the pane being typed into. The
        // focus was put back on the first pane, so the anchor is the
        // window's own content corner; taking the *last* pane in the list
        // instead - which is what a loop that overwrites the anchor every
        // iteration does - would put it half a window to the right.
        STD_INSIST(drive.anchor.count == 1);
        STD_INSIST(drive.anchor.x == 3 + composer.borderPixels());
        STD_INSIST(drive.anchor.y == 16 + composer.borderPixels());

        // No reaping - see the note in
        // HeadlessRunWiresPresentsAndTearsDownProductionComponents.
    }

    // R8-test, the hole T10 handed over: presentTerminal()'s
    // retained-output branch. Two lines of the T13 contract live there
    // and had nothing behind them - "a pane with nothing to say hands
    // over its retained form" and "consume() only for the panes that
    // were asked through output()".
    //
    // The frame under test has one pane speaking and one silent, and the
    // silent one is the *focused* one. That is what makes the branch
    // readable from outside ApplicationImpl: the input method's anchor is
    // taken from the focused pane's update, whichever of the two forms
    // that update came from, so a frame that skipped the silent pane
    // would reach requestTextInputRect() with no anchor at all and record
    // nothing. Hence the count is asserted as well as the position -
    // position alone repeats the previous frame's number and would hold
    // just as well if this frame had never anchored anything.
    //
    // consume() on the silent pane is guarded by the product itself:
    // Vterm::consume() asserts on updateScreen, which a retained form
    // never sets.
    STD_TEST(TheQuietPaneOfAFrameHandsOverItsRetainedFormAndKeepsTheAnchor) {
        SavedSignals savedSignals;
        keepMallocDebugOutOfTheChildren();
        ObjPool* const pool = ObjPool::fromMemoryRaw();
        Composer& composer = *pool->make<Composer>(pool);
        plt::Platform* const platform = plt::createHeadlessPlatform(*pool);
        composer.platform = platform;
        auto* const poller = static_cast<plt::PollerLoop*>(platform->poller());
        DriveApplication drive(composer);
        drive.splitPanes = true;
        drive.quietPaneProbe = true;
        StopOnTimeout timeout(*platform);
        poller->timeout(1, drive);
        poller->timeout(5'000'000, timeout);

        Application* const application = Application::create(composer);
        char program[] = "application_ut";
        char config[] = "-config";
        char configPath[] = "/dev/null";
        char panes[] = "-panes";
        char geometry[] = "-geometry";
        char geometryValue[] = "20x4";
        char execute[] = "-e";
        char shell[] = "/bin/sh";
        char commandFlag[] = "-c";
        char script[] = "IFS= read -r line; printf 'seen:%s\\n' \"$line\"";
        char* argv[] = {
            program,
            config,
            configPath,
            panes,
            geometry,
            geometryValue,
            execute,
            shell,
            commandFlag,
            script,
            nullptr,
        };

        const int result = application->run(10, argv);
        poller->cancel(timeout);

        STD_INSIST(result == 0);
        STD_INSIST(drive.split);
        STD_INSIST(!timeout.fired);

        // The pane really was silent going into the frame, and the frame
        // really was presented - without both of these the assertions
        // below are about some other frame than the one under test.
        STD_INSIST(drive.quietPaneDrained);
        STD_INSIST(drive.quietFramePresented);

        // The two panes are side by side, so the second one's anchor is a
        // different number from the first one's - which is what makes the
        // position worth asserting at all.
        STD_INSIST(drive.quietPaneOriginX > 0);
        STD_INSIST(drive.quietPaneOriginY == 0);

        // This frame anchored, and it anchored on the silent focused pane.
        STD_INSIST(drive.quietAnchor.count == drive.anchorsBeforeQuietFrame + 1);
        const Insets insets = composer.contentInsets();
        STD_INSIST(drive.quietAnchor.x == (i32)(insets.left) + drive.quietPaneOriginX);
        STD_INSIST(drive.quietAnchor.y == (i32)(insets.top) + drive.quietPaneOriginY);

        // No reaping - see the note in
        // HeadlessRunWiresPresentsAndTearsDownProductionComponents.
    }
}

// R4-test: the second debt handed over by name, from
// docs/reports/F3-wave3-findings-2026-08-19.md. The arithmetic behind
// the window's minimum size and resize increment is covered cell by cell
// in grid_geometry_ut.cpp; the hand-off is not. plt::Window takes each
// pair as two u32 scalars, so swapping them at the call site in
// ApplicationImpl::fontChanged() compiles and runs, and every backend
// but this one drops the request into a real window manager that obeys
// it silently - the mutation F3 named R14' and measured as green across
// the whole graph. The headless window now records both pairs, which is
// what makes the hand-off itself readable.
STD_TEST_SUITE(WindowSizingRequests) {
    STD_TEST(TheMinimumSizeAndTheResizeUnitPairEachAxisWithItsOwnInsets) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Options options;
        options.border = 0;
        options.nCols = 80;
        options.nRows = 24;
        composer.opts = &options;
        plt::Platform* const platform = plt::createHeadlessPlatform(*pool);
        composer.window = platform->createWindow(*pool, {});
        composer.setGlyphSize(8, 16);
        // Chrome on two edges, so the horizontal reserve and the
        // vertical one are different numbers and neither can stand in
        // for the other. Without a reserve on any side the two axes of
        // the reserve are both `2 * border` and a swap is invisible
        // again - which is exactly why this test could not be written
        // before wave 4.
        composer.setChromeReserve(ChromeSide::Right, 220);
        composer.setChromeReserve(ChromeSide::Top, 32);
        Application::create(composer);

        // What Composer does after a font is replaced or its size
        // changes: fontChanged() is reached through this list, never
        // called directly from outside.
        for (IntrusiveNode* node = composer.fontChangedListeners.mutFront(); node != composer.fontChangedListeners.mutEnd();) {
            Listener* const listener = static_cast<Listener*>(node);
            node = node->next;
            listener->onListen();
        }

        auto& window = static_cast<plt::WindowHeadless&>(*composer.window);
        const Insets insets = composer.contentInsets();

        STD_INSIST(insets.left == 0);
        STD_INSIST(insets.right == 220);
        STD_INSIST(insets.top == 32);
        STD_INSIST(insets.bottom == 0);

        const plt::WindowSizeRequest minimum = window.requestedMinimumSize();

        STD_INSIST(minimum.count == 1);
        // One cell plus the reserve, each axis out of its own two sides.
        STD_INSIST(minimum.width == gridPixelWidth(1, insets, composer.glyphWidth));
        STD_INSIST(minimum.height == gridPixelHeight(1, insets, composer.glyphHeight));
        STD_INSIST(minimum.width == 228);
        STD_INSIST(minimum.height == 48);

        const plt::WindowResizeUnitRequest unit = window.requestedResizeUnit();

        STD_INSIST(unit.count == 1);
        STD_INSIST(unit.width == composer.glyphWidth);
        STD_INSIST(unit.height == composer.glyphHeight);
        STD_INSIST(unit.baseWidth == gridPixelWidth(0, insets, composer.glyphWidth));
        STD_INSIST(unit.baseHeight == gridPixelHeight(0, insets, composer.glyphHeight));
        STD_INSIST(unit.baseWidth == 220);
        STD_INSIST(unit.baseHeight == 32);

        // And the window it asks for is the requested geometry with the
        // same two reserves put back, width from the horizontal pair.
        const plt::WindowInfo info = composer.window->info();

        STD_INSIST(info.width == gridPixelWidth(80, insets, composer.glyphWidth));
        STD_INSIST(info.height == gridPixelHeight(24, insets, composer.glyphHeight));
        STD_INSIST(info.width == 860);
        STD_INSIST(info.height == 416);
    }
}

// toggleQuickWindow() is the one entry point ui_quick_hotkey's Carbon
// handler calls; it is plain portable logic over plt::Window (no
// #if defined(__APPLE__) guard on its own definition in application.cpp),
// so the headless backend exercises the real thing here, not a stand-in.
// What it must not do - register a real global hotkey, touch a live
// NSWindow/focus - is out of scope by construction: this function never
// touches Carbon at all, that lives in ui_quick_hotkey.mm instead.
STD_TEST_SUITE(ToggleQuickWindow) {
    STD_TEST(NullWindowIsANoOp) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        STD_INSIST(composer.window == nullptr);

        toggleQuickWindow(composer);

        STD_INSIST(composer.window == nullptr);
    }

    STD_TEST(HiddenWindowIsShownByToggle) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        plt::Platform* const platform = plt::createHeadlessPlatform(*pool);
        composer.window = platform->createWindow(*pool, {});
        STD_INSIST(!composer.window->visible());

        toggleQuickWindow(composer);

        STD_INSIST(composer.window->visible());
    }

    STD_TEST(VisibleWindowIsHiddenByToggle) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        plt::Platform* const platform = plt::createHeadlessPlatform(*pool);
        composer.window = platform->createWindow(*pool, {});
        composer.window->requestShow();
        STD_INSIST(composer.window->visible());

        toggleQuickWindow(composer);

        STD_INSIST(!composer.window->visible());
    }

    // Regression test for the finding R1-qa passed to T3 (see
    // docs/reports/T3-window-chrome-2026-08-17.md): a toggle that only
    // asks visible() would hide an already-Dock-minimized window instead
    // of restoring it, because a minimized NSWindow still answers
    // isVisible with true. toggleQuickWindow() must also check
    // info().iconified and take the show branch instead.
    STD_TEST(IconifiedVisibleWindowIsShownRatherThanHiddenByToggle) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        plt::Platform* const platform = plt::createHeadlessPlatform(*pool);
        composer.window = platform->createWindow(*pool, {});
        composer.window->requestShow();
        composer.window->requestIconify();
        STD_INSIST(composer.window->visible());
        STD_INSIST(composer.window->info().iconified);

        toggleQuickWindow(composer);

        STD_INSIST(composer.window->visible());
    }

    // A6: a saved frame wins over the default placement once
    // quickRememberFrame is on and a state file exists.
    // WindowHeadlessImpl starts every window at x=10, y=20, 800x600
    // (its constructor) and requestShowAt() on this backend never
    // changes geometry (it falls through to requestShow(), see
    // platform_headless.cpp) - so unaffected geometry after a show is
    // exactly this backend's real default, not a stand-in value.
    STD_TEST(ShowAppliesTheSavedFrameOverTheDefaultPlacement) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        plt::Platform* const platform = plt::createHeadlessPlatform(*pool);
        composer.window = platform->createWindow(*pool, {});

        StringBuilder dir;
        makeTempDir(dir);
        StringBuilder configPath;
        configPath << StringView(dir) << StringView(u8"/config.toml");
        StringBuilder framePath;
        STD_INSIST(defaultQuickFramePath(StringView(configPath), framePath));
        const QuickFrame saved{.x = 100, .y = 50, .width = 640, .height = 480};
        STD_INSIST(saveQuickFrame(StringView(framePath), saved));

        Options options;
        options.quickRememberFrame = true;
        options.configPath = StringView(configPath);
        composer.opts = &options;

        toggleQuickWindow(composer);

        const plt::WindowInfo info = composer.window->info();
        STD_INSIST(info.x == 100);
        STD_INSIST(info.y == 50);
        STD_INSIST(info.width == 640);
        STD_INSIST(info.height == 480);

        unlink(framePath.cStr());
        rmdir(dir.cStr());
    }

    STD_TEST(ShowIgnoresASavedFrameWhenQuickRememberFrameIsOff) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        plt::Platform* const platform = plt::createHeadlessPlatform(*pool);
        composer.window = platform->createWindow(*pool, {});

        StringBuilder dir;
        makeTempDir(dir);
        StringBuilder configPath;
        configPath << StringView(dir) << StringView(u8"/config.toml");
        StringBuilder framePath;
        STD_INSIST(defaultQuickFramePath(StringView(configPath), framePath));
        const QuickFrame saved{.x = 100, .y = 50, .width = 640, .height = 480};
        STD_INSIST(saveQuickFrame(StringView(framePath), saved));

        Options options;
        options.quickRememberFrame = false;
        options.configPath = StringView(configPath);
        composer.opts = &options;

        toggleQuickWindow(composer);

        const plt::WindowInfo info = composer.window->info();
        STD_INSIST(info.x == 10);
        STD_INSIST(info.y == 20);
        STD_INSIST(info.width == 800);
        STD_INSIST(info.height == 600);

        unlink(framePath.cStr());
        rmdir(dir.cStr());
    }

    // A6: no saved file falls back to the default placement, exactly the
    // same as before quickRememberFrame existed - deleting the state
    // file is the documented reset.
    STD_TEST(ShowWithNoSavedFileKeepsTheDefaultPlacement) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        plt::Platform* const platform = plt::createHeadlessPlatform(*pool);
        composer.window = platform->createWindow(*pool, {});

        Options options;
        options.quickRememberFrame = true;
        options.configPath = StringView(u8"/nonexistent/config.toml");
        composer.opts = &options;

        toggleQuickWindow(composer);

        const plt::WindowInfo info = composer.window->info();
        STD_INSIST(info.x == 10);
        STD_INSIST(info.y == 20);
        STD_INSIST(info.width == 800);
        STD_INSIST(info.height == 600);
    }

    // A6's clamp-into-the-current-screen: WindowHeadlessImpl's
    // constructor sets a simulated 1920x1080 screen, unrelated to the
    // window's own placement above.
    STD_TEST(ShowClampsASavedFrameLargerThanTheScreen) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        plt::Platform* const platform = plt::createHeadlessPlatform(*pool);
        composer.window = platform->createWindow(*pool, {});

        StringBuilder dir;
        makeTempDir(dir);
        StringBuilder configPath;
        configPath << StringView(dir) << StringView(u8"/config.toml");
        StringBuilder framePath;
        STD_INSIST(defaultQuickFramePath(StringView(configPath), framePath));
        const QuickFrame saved{.x = 5000, .y = -500, .width = 5000, .height = 5000};
        STD_INSIST(saveQuickFrame(StringView(framePath), saved));

        Options options;
        options.quickRememberFrame = true;
        options.configPath = StringView(configPath);
        composer.opts = &options;

        toggleQuickWindow(composer);

        const plt::WindowInfo info = composer.window->info();
        STD_INSIST(info.width == 1920);
        STD_INSIST(info.height == 1080);
        STD_INSIST(info.x == 0);
        STD_INSIST(info.y == 0);

        unlink(framePath.cStr());
        rmdir(dir.cStr());
    }

    // F2's report, I1: the four tests above cannot tell a points clamp
    // from a pixels clamp, because at contentScale = 1 they are
    // numerically the same bound - ShowClampsASavedFrameLargerThanTheScreen
    // stayed green through the exact B1 mutation that broke this on any
    // Retina display. WindowHeadlessImpl::configure() lets a headless
    // window carry a real contentScale, so this covers B1 without a live
    // NSWindow at all.
    STD_TEST(ShowDoesNotHalveAFullScreenSavedFrameAtDoubleScale) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        plt::Platform* const platform = plt::createHeadlessPlatform(*pool);
        composer.window = platform->createWindow(*pool, {});
        plt::WindowHeadless& headlessWindow = static_cast<plt::WindowHeadless&>(*composer.window);
        headlessWindow.configure({
            .width = 800,
            .height = 600,
            .screenPixelWidth = 3840,
            .screenPixelHeight = 2160,
            .contentScale = 2.0f,
        });

        StringBuilder dir;
        makeTempDir(dir);
        StringBuilder configPath;
        configPath << StringView(dir) << StringView(u8"/config.toml");
        StringBuilder framePath;
        STD_INSIST(defaultQuickFramePath(StringView(configPath), framePath));
        // The screen's own size in points - 3840x2160 backing pixels at
        // scale 2. Reading this saved size as backing pixels (B1's bug,
        // and what the pre-B4 file format actually stored) would divide
        // it by the scale and restore half a screen.
        const QuickFrame saved{.x = 0, .y = 0, .width = 1920, .height = 1080};
        STD_INSIST(saveQuickFrame(StringView(framePath), saved));

        Options options;
        options.quickRememberFrame = true;
        options.configPath = StringView(configPath);
        composer.opts = &options;

        toggleQuickWindow(composer);

        const plt::WindowInfo info = composer.window->info();
        STD_INSIST(info.width == 3840);
        STD_INSIST(info.height == 2160);
        STD_INSIST(info.x == 0);
        STD_INSIST(info.y == 0);

        unlink(framePath.cStr());
        rmdir(dir.cStr());
    }

    STD_TEST(ShowClampsASavedFramePositionInPointsNotPixelsAtDoubleScale) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        plt::Platform* const platform = plt::createHeadlessPlatform(*pool);
        composer.window = platform->createWindow(*pool, {});
        plt::WindowHeadless& headlessWindow = static_cast<plt::WindowHeadless&>(*composer.window);
        headlessWindow.configure({
            .width = 800,
            .height = 600,
            .screenPixelWidth = 3840,
            .screenPixelHeight = 2160,
            .contentScale = 2.0f,
        });

        StringBuilder dir;
        makeTempDir(dir);
        StringBuilder configPath;
        configPath << StringView(dir) << StringView(u8"/config.toml");
        StringBuilder framePath;
        STD_INSIST(defaultQuickFramePath(StringView(configPath), framePath));
        // The screen is 3840x2160 backing pixels, 1920x1080 points at
        // scale 2. A saved 960x540-point window at x=1000 has its right
        // edge at 1960, past the 1920-point-wide screen; the correct
        // bound in points is 1920-960=960. A clamp computed in pixels
        // (B1's bug) would compare x=1000 against a 3840-wide bound and
        // never clamp it at all.
        const QuickFrame saved{.x = 1000, .y = 50, .width = 960, .height = 540};
        STD_INSIST(saveQuickFrame(StringView(framePath), saved));

        Options options;
        options.quickRememberFrame = true;
        options.configPath = StringView(configPath);
        composer.opts = &options;

        toggleQuickWindow(composer);

        const plt::WindowInfo info = composer.window->info();
        STD_INSIST(info.width == 1920);
        STD_INSIST(info.height == 1080);
        STD_INSIST(info.x == 960);
        STD_INSIST(info.y == 50);

        unlink(framePath.cStr());
        rmdir(dir.cStr());
    }

    // The other half of what applySavedQuickFrame() decides: which of
    // its two paths runs at all. applyQuickFrameToWindow() is what
    // answers that, and its answer is the backend tag - not the .window
    // pointer, which every backend fills in (the headless one with its
    // own render target). Bridging that pointer and messaging it took a
    // whole test binary down twice on this wave (F2's own report, and
    // R2-qa round 2's B5 in the sibling file). The tests around this one
    // would notice such a regression only as a SIGSEGV somewhere in the
    // middle of the suite; this one names it, and pins that a refused
    // window is left exactly as it was rather than half-moved.
    //
    // ui_quick_hotkey.mm is macOS-only in build.py, so off darwin there
    // is nothing to link against - the same guard applySavedQuickFrame()
    // itself now carries (R2-test, L1).
#if defined(__APPLE__)
    STD_TEST(ApplyingASavedFrameRefusesANonCocoaBackend) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        plt::Platform* const platform = plt::createHeadlessPlatform(*pool);
        composer.window = platform->createWindow(*pool, {});
        STD_INSIST(composer.window->renderContext().backend != plt::RenderBackend::Cocoa);
        STD_INSIST(composer.window->renderContext().window != nullptr);
        const plt::WindowInfo before = composer.window->info();

        STD_INSIST(!applyQuickFrameToWindow(composer, {.x = 100, .y = 50, .width = 640, .height = 480}));

        const plt::WindowInfo after = composer.window->info();
        STD_INSIST(after.x == before.x);
        STD_INSIST(after.y == before.y);
        STD_INSIST(after.width == before.width);
        STD_INSIST(after.height == before.height);
    }

    STD_TEST(ApplyingASavedFrameRefusesAComposerWithNoWindow) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        STD_INSIST(composer.window == nullptr);

        STD_INSIST(!applyQuickFrameToWindow(composer, {.x = 100, .y = 50, .width = 640, .height = 480}));
    }
#endif

    // R2-qa round 2, B4: one state file, two displays of different
    // scale. A frame saved in backing pixels meant a different window on
    // each of them - measured as a 1000x500-point window coming back
    // 2000x968 on the 1x monitor and 522x262 back on the 2x panel, with
    // the corrupted value persisted in between. Stored in points, the
    // very same file has to describe the very same window on both: the
    // position identical, the size identical once each screen's own
    // scale is undone.
    STD_TEST(ShowRestoresTheSameFrameOnScreensOfDifferentScale) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        plt::Platform* const platform = plt::createHeadlessPlatform(*pool);

        StringBuilder dir;
        makeTempDir(dir);
        StringBuilder configPath;
        configPath << StringView(dir) << StringView(u8"/config.toml");
        StringBuilder framePath;
        STD_INSIST(defaultQuickFramePath(StringView(configPath), framePath));
        const QuickFrame saved{.x = 300, .y = 200, .width = 640, .height = 480};
        STD_INSIST(saveQuickFrame(StringView(framePath), saved));

        Options options;
        options.quickRememberFrame = true;
        options.configPath = StringView(configPath);
        composer.opts = &options;

        // The 1x monitor: 1920x1080 backing pixels is also 1920x1080
        // points, WindowHeadlessImpl's own default.
        composer.window = platform->createWindow(*pool, {});
        toggleQuickWindow(composer);
        const plt::WindowInfo single = composer.window->info();

        // The 2x panel: 3840x2160 backing pixels is the same 1920x1080
        // points of usable screen.
        composer.window = platform->createWindow(*pool, {});
        static_cast<plt::WindowHeadless&>(*composer.window)
            .configure({
                .width = 800,
                .height = 600,
                .screenPixelWidth = 3840,
                .screenPixelHeight = 2160,
                .contentScale = 2.0f,
            });
        toggleQuickWindow(composer);
        const plt::WindowInfo doubled = composer.window->info();

        STD_INSIST(single.x == 300);
        STD_INSIST(single.y == 200);
        STD_INSIST(single.width == 640);
        STD_INSIST(single.height == 480);
        // Same position in points, same size in points - which is twice
        // as many backing pixels on the 2x screen, not half as many.
        STD_INSIST(doubled.x == single.x);
        STD_INSIST(doubled.y == single.y);
        STD_INSIST(doubled.width == single.width * 2);
        STD_INSIST(doubled.height == single.height * 2);

        unlink(framePath.cStr());
        rmdir(dir.cStr());
    }
}
