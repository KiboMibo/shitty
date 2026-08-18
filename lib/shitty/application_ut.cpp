/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "application.h"

#include "composer.h"
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
}
