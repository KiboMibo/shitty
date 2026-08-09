/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "composer.h"
#include "listener.h"
#include "pty.h"
#include "session.h"
#include "startup.h"
#include "vterm_headless.h"

#include <plt/fiber.h>
#include <plt/loop_wake.h>
#include <plt/platform.h>
#include <plt/poller_loop.h>

#include <std/ios/output.h>
#include <std/ios/input.h>
#include <std/mem/obj_pool.h>
#include <std/thr/runable.h>
#include <std/tst/ut.h>

#include <sys/wait.h>

using namespace stl;

namespace {
    constexpr u64 testTimeoutUs = 5'000'000;

    struct Timeout final: public plt::TimerCallback {
        void ready() override {
            fired = true;
        }

        bool fired = false;
    };

    struct WakeMarker final: public plt::TimerCallback {
        void ready() override {
            delivered = true;
        }

        bool delivered = false;
    };

    struct ParkInput final: public Input {
        explicit ParkInput(plt::Scheduler& scheduler_)
            : scheduler(scheduler_)
        {
        }

        size_t readImpl(void*, size_t) override {
            scheduler.current()->park();
            return 0;
        }

        plt::Scheduler& scheduler;
    };

    struct SurvivorHandle final: public PtyHandle {
        explicit SurvivorHandle(Composer& composer_)
            : composer(composer_)
            , input_(*composer.platform->scheduler())
        {
        }

        Input* input() override {
            return &input_;
        }

        Output* output() override {
            return composer.ptyOutput;
        }

        void resize(const PtySize&) override {
        }

        Composer& composer;
        ParkInput input_;
    };

    struct TwoSessionPty final: public Pty {
        TwoSessionPty(Composer& composer_, Pty& real_)
            : composer(composer_)
            , real(real_)
        {
        }

        PtyHandle* spawn(ObjPool& owner, const LaunchCommand& command) override {
            if (spawns++ == 0) {
                doomed = real.spawn(owner, command);
                return doomed;
            }
            return owner.make<SurvivorHandle>(composer);
        }

        Composer& composer;
        Pty& real;
        PtyHandle* doomed = nullptr;
        size_t spawns = 0;
    };

    void publish(IntrusiveList& listeners) {
        for (IntrusiveNode* node = listeners.mutFront(); node != listeners.mutEnd();) {
            Listener* const listener = static_cast<Listener*>(node);
            node = node->next;
            listener->onListen();
        }
    }
}

STD_TEST_SUITE(Pty) {
    // EOF in one of two sessions ends the client-owned read fiber. The
    // session arena is then deleted on the deferred EOF wake, and the loop
    // must still be able to dispatch another independent wake afterwards.
    STD_TEST(EofClosesOneSessionBeforeItsFollowupWake) {
        ObjPool::Ref pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        VtermHeadless* const host = VtermHeadless::create(composer, nullptr);
        (void)(host);

        char program[] = "pty_ut";
        char execute[] = "-e";
        char shell[] = "/bin/sh";
        char commandFlag[] = "-c";
        char commandText[] = "read ignored; exit 0";
        char* argv[] = {program, execute, shell, commandFlag, commandText, nullptr};
        const LaunchCommand command = buildLaunchCommand(5, argv, StringView(), false);
        Pty* const real = createPty(*composer.pool, *composer.platform->scheduler());
        TwoSessionPty pty(composer, *real);
        composer.pty = &pty;
        composer.launch = &command;
        SessionSet* const sessions = SessionSet::create(composer);
        publish(composer.newTabListeners);
        publish(composer.prevTabListeners);

        // EOT makes the shell's canonical read return EOF, just like Ctrl+D.
        const u8 eot = 0x04;
        pty.doomed->output()->write(&eot, 1);
        pty.doomed->output()->flush();

        auto* const poller = static_cast<plt::PollerLoop*>(composer.platform->poller());
        Timeout closeTimeout;
        poller->timeout(testTimeoutUs, closeTimeout);
        while (SessionSet::liveSessions != 1 && !closeTimeout.fired) {
            poller->dispatchTimers();
            if (SessionSet::liveSessions != 1 && !closeTimeout.fired) {
                poller->wait(poller->nextDeadline());
            }
        }
        poller->cancel(closeTimeout);
        STD_INSIST(SessionSet::liveSessions == 1);
        STD_INSIST(sessions->activeTerminal() != nullptr);
        STD_INSIST(!closeTimeout.fired);

        // The EOF callback has removed the tab and its arena, including
        // the finished reader's owned handle and stack.
        plt::Scheduler* const scheduler = composer.platform->scheduler();
        plt::Fiber* sentinelFiber = nullptr;
        bool sentinelWoke = false;
        auto sentinel = makeRunable([&] {
            sentinelFiber = scheduler->current();
            sentinelFiber->park();
            sentinelWoke = true;
        });
        sentinelFiber = scheduler->create(*composer.pool, sentinel);
        STD_INSIST(sentinelFiber != nullptr);
        STD_INSIST(!sentinelWoke);

        // This is deliberately a later loop wake, after the session pool
        // was removed, rather than merely observing the EOF callback.
        WakeMarker marker;
        plt::LoopWake* const markerWake = composer.platform->createLoopWake(*composer.pool, marker);
        markerWake->signal();
        Timeout wakeTimeout;
        poller->timeout(testTimeoutUs, wakeTimeout);
        while (!marker.delivered && !wakeTimeout.fired) {
            poller->dispatchTimers();
            if (!marker.delivered && !wakeTimeout.fired) {
                poller->wait(poller->nextDeadline());
            }
        }
        poller->cancel(wakeTimeout);

        const bool wokeUnrelatedFiber = sentinelWoke;
        if (!wokeUnrelatedFiber) {
            sentinelFiber->release();
        }
        STD_INSIST(marker.delivered);
        STD_INSIST(!wakeTimeout.fired);
        STD_INSIST(!wokeUnrelatedFiber);

        int status = 0;
        const pid_t child = waitpid(-1, &status, 0);
        STD_INSIST(child > 0);
        STD_INSIST(WIFEXITED(status));
        STD_INSIST(WEXITSTATUS(status) == 0);
    }
}
