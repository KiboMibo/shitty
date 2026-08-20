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
#include "session.h"
#include "ui_quick_hotkey.h"

#include <plt/input.h>
#include <plt/platform.h>
#include <plt/platform_headless.h>
#include <plt/poller.h>
#include <plt/poller_loop.h>
#include <plt/window.h>

#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/tst/ut.h>

#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
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
            framePresented = window.dispatchFrame();

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
        }

        Composer& composer;
        bool fired = false;
        bool framePresented = false;
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

        // The shell outlives run(): its session is gone, but whether the
        // product's own SIGCHLD handler reaped it depends on whether it
        // died before savedSignals put the default disposition back. A
        // child left unreaped here becomes a zombie the rest of the binary
        // inherits - which is exactly what the Pty suite's waits used to
        // pick up instead of their own. Drain it while it is still ours.
        while (waitpid(-1, nullptr, 0) > 0) {
        }
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
