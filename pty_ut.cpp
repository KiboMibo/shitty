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

#include <signal.h>
#include <stdlib.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

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

        void engage() override {
        }

        size_t acquire(StringView*, size_t) override {
            for (;;) {
                composer.platform->scheduler()->current()->park();
            }
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

    struct RealPtyFixture {
        RealPtyFixture()
            : pool(ObjPool::fromMemory())
            , poller(plt::PollerLoop::create(*pool))
            , scheduler(plt::Scheduler::create(*pool, *poller))
            , pty(createPty(*pool, *scheduler))
        {
        }

        ObjPool::Ref pool;
        plt::PollerLoop* poller;
        plt::Scheduler* scheduler;
        Pty* pty;
    };

    PtyHandle* spawnShell(Pty& pty, ObjPool& owner, char* script) {
        char program[] = "pty_ut";
        char execute[] = "-e";
        char shell[] = "/bin/sh";
        char commandFlag[] = "-c";
        char* argv[] = {program, execute, shell, commandFlag, script, nullptr};
        const LaunchCommand command = buildLaunchCommand(5, argv, StringView(), false);
        return pty.spawn(owner, command);
    }

    PtyHandle* spawnHelper(Pty& pty, ObjPool& owner, char* mode) {
        char program[] = "pty_ut";
        char execute[] = "-e";
        char* const helper = getenv("SHITTY_PTY_TEST_HELPER");
        STD_INSIST(helper != nullptr);
        char* argv[] = {program, execute, helper, mode, nullptr};
        const LaunchCommand command = buildLaunchCommand(4, argv, StringView(), false);
        return pty.spawn(owner, command);
    }

    std::string readAll(PtyHandle& handle) {
        std::string result;
        char buffer[4096];
        for (;;) {
            const size_t count = handle.input()->read(buffer, sizeof(buffer));
            if (count == 0) {
                return result;
            }
            result.append(buffer, count);
        }
    }

    std::string readUntil(PtyHandle& handle, const char* needle) {
        std::string result;
        char buffer[256];
        while (result.find(needle) == std::string::npos) {
            const size_t count = handle.input()->read(buffer, sizeof(buffer));
            STD_INSIST(count != 0);
            result.append(buffer, count);
        }
        return result;
    }

    int reapChild() {
        int status = 0;
        const pid_t child = waitpid(-1, &status, 0);
        STD_INSIST(child > 0);
        return status;
    }
}

STD_TEST_SUITE(Pty) {
    STD_TEST(ChildOutputReachesEof) {
        RealPtyFixture fixture;
        ObjPool* const owner = ObjPool::fromMemoryRaw();
        char script[] = "printf pty-output";
        PtyHandle* const handle = spawnShell(*fixture.pty, *owner, script);

        const std::string output = readAll(*handle);
        delete owner;
        const int status = reapChild();

        STD_INSIST(output == "pty-output");
        STD_INSIST(WIFEXITED(status));
        STD_INSIST(WEXITSTATUS(status) == 0);
    }

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
        Pty* const real = createPty(*composer.pool, *composer.platform->scheduler(), composer.platform);
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

    STD_TEST(InputRoundTripsThroughTheSlave) {
        RealPtyFixture fixture;
        ObjPool* const owner = ObjPool::fromMemoryRaw();
        char script[] = "stty -echo; IFS= read -r line; printf 'got:%s\\n' \"$line\"";
        PtyHandle* const handle = spawnShell(*fixture.pty, *owner, script);

        const char input[] = "hello from master\n";
        handle->output()->write(input, sizeof(input) - 1);
        const std::string output = readAll(*handle);
        delete owner;
        const int status = reapChild();

        STD_INSIST(output.find("got:hello from master") != std::string::npos);
        STD_INSIST(WIFEXITED(status));
        STD_INSIST(WEXITSTATUS(status) == 0);
    }

    STD_TEST(LargeChildOutputSurvivesBackpressure) {
        RealPtyFixture fixture;
        ObjPool* const owner = ObjPool::fromMemoryRaw();
        char script[] = "head -c 1048576 /dev/zero";
        PtyHandle* const handle = spawnShell(*fixture.pty, *owner, script);

        // Let the child fill the finite slave-to-master queue before the
        // first read, then drain it through repeated readiness waits.
        usleep(50'000);
        size_t total = 0;
        size_t nonzero = 0;
        u8 buffer[8192];
        for (;;) {
            const size_t count = handle->input()->read(buffer, sizeof(buffer));
            if (count == 0) {
                break;
            }
            total += count;
            for (size_t index = 0; index < count; ++index) {
                nonzero += buffer[index] != 0;
            }
        }
        delete owner;
        const int status = reapChild();

        STD_INSIST(total == 1024 * 1024);
        STD_INSIST(nonzero == 0);
        STD_INSIST(WIFEXITED(status));
        STD_INSIST(WEXITSTATUS(status) == 0);
    }

    STD_TEST(ResizeReachesChildAsWinch) {
        RealPtyFixture fixture;
        ObjPool* const owner = ObjPool::fromMemoryRaw();
        char mode[] = "winsize";
        PtyHandle* const handle = spawnHelper(*fixture.pty, *owner, mode);

        const std::string ready = readUntil(*handle, "\n");
        handle->resize({
            .columns = 123,
            .rows = 47,
            .pixelWidth = 984,
            .pixelHeight = 752,
        });
        const std::string output = readAll(*handle);
        delete owner;
        const int status = reapChild();

        STD_INSIST(ready.find("ready") != std::string::npos);
        STD_INSIST(output.find("47 123") != std::string::npos);
        STD_INSIST(WIFEXITED(status));
        STD_INSIST(WEXITSTATUS(status) == 0);
    }

    // The engaged path's hairy exit: the arena dies while the drain is
    // mid-flood and the feed holds acquired blocks. The destructor's
    // handshake must balance the ledger and hang up the child.
    STD_TEST(EngagedOwnerDeathSurvivesAFloodingChild) {
        ObjPool::Ref pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        VtermHeadless* const host = VtermHeadless::create(composer, nullptr);
        (void)(host);
        Pty* const pty = createPty(*composer.pool, *composer.platform->scheduler(), composer.platform);
        ObjPool* const owner = ObjPool::fromMemoryRaw();
        char script[] = "yes engaged-flood";
        PtyHandle* const handle = spawnShell(*pty, *owner, script);
        handle->engage();

        size_t consumed = 0;
        auto feed = makeRunable([&] {
            StringView slices[8];
            for (;;) {
                const size_t count = handle->acquire(slices, 8);
                if (count == 0) {
                    return;
                }
                for (size_t index = 0; index < count; ++index) {
                    consumed += slices[index].length();
                }
            }
        });
        composer.platform->scheduler()->create(*owner, feed, 64 * 1024);

        auto* const poller = static_cast<plt::PollerLoop*>(composer.platform->poller());
        Timeout floodTimeout;
        poller->timeout(testTimeoutUs, floodTimeout);
        while (consumed < 512 * 1024 && !floodTimeout.fired) {
            poller->dispatchTimers();
            if (consumed < 512 * 1024 && !floodTimeout.fired) {
                poller->wait(poller->nextDeadline());
            }
        }
        poller->cancel(floodTimeout);
        STD_INSIST(!floodTimeout.fired);

        // Mid-flood: the feed fiber is released first (LIFO), then the
        // handle walks the two-phase goodbye with the drain.
        delete owner;
        const int status = reapChild();
        STD_INSIST(WIFSIGNALED(status));
        STD_INSIST(WTERMSIG(status) == SIGHUP);
    }

    STD_TEST(OwnerDeathReleasesBlockedIoAndHangsUpChild) {
        RealPtyFixture fixture;
        ObjPool* const owner = ObjPool::fromMemoryRaw();
        char mode[] = "hangup";
        PtyHandle* const handle = spawnHelper(*fixture.pty, *owner, mode);
        (void)(readUntil(*handle, "\n"));

        bool readerReturned = false;
        auto reader = makeRunable([&] {
            u8 byte = 0;
            handle->input()->read(&byte, 1);
            readerReturned = true;
        });
        fixture.scheduler->create(*owner, reader);
        STD_INSIST(!readerReturned);

        std::string input(1024 * 1024, 'x');
        bool writerReturned = false;
        auto writer = makeRunable([&] {
            handle->output()->write(input.data(), input.size());
            writerReturned = true;
        });
        fixture.scheduler->create(*owner, writer, 64 * 1024);
        STD_INSIST(!writerReturned);

        // LIFO pool teardown releases both client-owned fibers before the
        // handle closes the master and sends SIGHUP. A later poll round sees
        // only scheduler tombstones, never the freed stacks.
        delete owner;
        fixture.poller->wait(0);
        const int status = reapChild();

        STD_INSIST(!readerReturned);
        STD_INSIST(!writerReturned);
        STD_INSIST(WIFSIGNALED(status));
        STD_INSIST(WTERMSIG(status) == SIGHUP);
    }
}
