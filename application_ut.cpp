/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "application.h"

#include "composer.h"
#include "session.h"
#include "ui_quick_hotkey.h"

#include <plt/input.h>
#include <plt/platform.h>
#include <plt/platform_headless.h>
#include <plt/poller.h>
#include <plt/poller_loop.h>
#include <plt/window.h>

#include <std/mem/obj_pool.h>
#include <std/str/view.h>
#include <std/tst/ut.h>

#include <signal.h>

using namespace stl;

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
        ObjPool::Ref pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
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
}
