/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "composer.h"
#include <lib/vterm/listener.h>
#include "options.h"
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
#include <std/mem/small_obj_allocator.h>
#include <std/thr/runable.h>
#include <std/tst/ut.h>

#include <signal.h>
#include <stdio.h>
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

    // A trivially owned chunk for the fakes: header and payload in one
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

    PtyHandle::Chunk* makeStubChunk(SmallObjAllocator& allocator, size_t len) {
        constexpr size_t cap = smallObjMaxSize - sizeof(StubChunk);
        const size_t granted = len < cap ? len : cap;
        auto* const chunk = new (allocator.allocate(sizeof(StubChunk) + granted)) StubChunk;
        chunk->owner = &allocator;
        chunk->allocated = (u32)(sizeof(StubChunk) + granted);
        chunk->used = (u32)(granted);
        return chunk;
    }

    struct SurvivorHandle final: public PtyHandle {
        explicit SurvivorHandle(Composer& composer_)
            : composer(composer_)
        {
        }

        void resize(const PtySize&) override {
        }

        void engage() override {
        }

        Chunk* allocate(size_t len) override {
            return makeStubChunk(*composer.smallObjects, len);
        }

        void send(Chunk* chunk, size_t) override {
            auto* const block = static_cast<StubChunk*>(chunk);
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
    };

    struct TwoSessionPty final: public Pty {
        TwoSessionPty(Composer& composer_, Pty& real_)
            : composer(composer_)
            , real(real_)
        {
        }

        PtyHandle* spawn(ObjPool& owner, const LaunchCommand& command, const PtySize& size) override {
            if (spawns++ == 0) {
                doomed = real.spawn(owner, command, size);
                return doomed;
            }
            return owner.make<SurvivorHandle>(composer);
        }

        Composer& composer;
        Pty& real;
        PtyHandle* doomed = nullptr;
        size_t spawns = 0;
    };

    // Everything the session's own reader takes off a real handle, copied
    // aside: the sessions own their handles, so this is the only place a
    // test can hear what a pane's child said.
    struct TeeHandle final: public PtyHandle {
        TeeHandle(PtyHandle& inner_, std::string& heard_)
            : inner(inner_)
            , heard(heard_)
        {
        }

        pid_t childPid() override {
            return inner.childPid();
        }

        void resize(const PtySize& size) override {
            inner.resize(size);
        }

        void engage() override {
            inner.engage();
        }

        Chunk* allocate(size_t len) override {
            return inner.allocate(len);
        }

        void send(Chunk* chunk, size_t len) override {
            inner.send(chunk, len);
        }

        Chunk* acquire() override {
            Chunk* const chunks = inner.acquire();
            for (Chunk* chunk = chunks; chunk != nullptr; chunk = chunk->next()) {
                heard.append((const char*)(chunk->data()), chunk->length());
            }
            return chunks;
        }

        void release(Chunk* chunks) override {
            inner.release(chunks);
        }

        PtyHandle& inner;
        std::string& heard;
    };

    // The child FirstLineDeadline is watching, and all a signal handler
    // is allowed to know.
    volatile sig_atomic_t deadlineChild = -1;

    void endFirstLineWait(int) {
        if (deadlineChild > 0) {
            kill((pid_t)(deadlineChild), SIGKILL);
        }
    }

    // A deadline on the wait below, because nothing under it has one:
    // acquire() on a handle nobody engaged is a blocking read, and its
    // EAGAIN path parks in poll(..., -1). A child that lives and says
    // nothing therefore hangs the whole unit_tests binary instead of
    // failing it, which in CI is a stuck job whose log does not say
    // which test is to blame. The child side of this same pty already
    // carries the same reasoning and the same remedy - alarm(10) in
    // tst/pty_test_helper.c, from R2-test I11: "A bounded wait keeps
    // that a test failure, which is what it is." This is the parent's
    // half of it.
    //
    // The child reports before it does anything else after exec, so the
    // file's own hung-test timeout is orders of magnitude more than this
    // wait ever needs. On expiry the child is killed, the master reaches
    // EOF, and readFirstLine() comes back with whatever it has - a
    // partial line reddens parseWinsize on its own.
    struct FirstLineDeadline {
        explicit FirstLineDeadline(pid_t child) {
            deadlineChild = child;
            previous = signal(SIGALRM, endFirstLineWait);
            alarm((unsigned)(testTimeoutUs / 1'000'000));
        }

        ~FirstLineDeadline() {
            alarm(0);
            signal(SIGALRM, previous);
            deadlineChild = -1;
        }

        void (*previous)(int) = nullptr;
    };

    // One line off a handle nobody has engaged yet, which is a blocking
    // read straight off the pty. A dead child ends the wait as an empty
    // string rather than as a hang: what it did or did not say is the
    // caller's assertion to make. A live but silent one is what
    // FirstLineDeadline above is for.
    void readFirstLine(PtyHandle& handle, std::string& into) {
        while (into.find('\n') == std::string::npos) {
            PtyHandle::Chunk* const chunks = handle.acquire();
            if (chunks == nullptr) {
                return;
            }
            for (PtyHandle::Chunk* chunk = chunks; chunk != nullptr; chunk = chunk->next()) {
                into.append((const char*)(chunk->data()), chunk->length());
            }
            handle.release(chunks);
        }
    }

    // Two panes' worth of the size each child was born with and of what
    // that child then said. Fixed slots rather than a vector because the
    // handles hold references into them for the pool's lifetime.
    struct BornSizePty final: public Pty {
        explicit BornSizePty(Pty& real_)
            : real(real_)
        {
        }

        PtyHandle* spawn(ObjPool& owner, const LaunchCommand& command, const PtySize& size) override {
            STD_INSIST(spawns < 2);
            born[spawns] = size;
            PtyHandle* const inner = real.spawn(owner, command, size);
            // The child's first line is taken here, still inside spawn(),
            // and not from the poller once the split has finished.
            // openSession() returns into applyLayout(), which resizes
            // every pane of the new layout - the newborn one included -
            // while the child still has a whole exec() to get through
            // before its first TIOCGWINSZ. The parent wins that race
            // every time, so a line read any later reports the size the
            // parent set *after* the fork, and a slave that was never
            // sized before the fork answers exactly the same. Reading
            // here puts the observation ahead of that resize, which is
            // the only moment at which the two states differ.
            const FirstLineDeadline deadline(inner->childPid());
            readFirstLine(*inner, heard[spawns]);
            return owner.make<TeeHandle>(*inner, heard[spawns++]);
        }

        Pty& real;
        PtySize born[2];
        std::string heard[2];
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
        return pty.spawn(owner, command, PtySize{});
    }

    PtyHandle* spawnHelper(Pty& pty, ObjPool& owner, char* mode, const PtySize& size = PtySize{}) {
        char program[] = "pty_ut";
        char execute[] = "-e";
        char* const helper = getenv("SHITTY_PTY_TEST_HELPER");
        STD_INSIST(helper != nullptr);
        char* argv[] = {program, execute, helper, mode, nullptr};
        const LaunchCommand command = buildLaunchCommand(4, argv, StringView(), false);
        return pty.spawn(owner, command, size);
    }

    std::string readAll(PtyHandle& handle) {
        std::string result;
        for (;;) {
            PtyHandle::Chunk* const chunks = handle.acquire();
            if (chunks == nullptr) {
                return result;
            }
            for (PtyHandle::Chunk* chunk = chunks; chunk != nullptr; chunk = chunk->next()) {
                result.append((const char*)(chunk->data()), chunk->length());
            }
            handle.release(chunks);
        }
    }

    std::string readUntil(PtyHandle& handle, const char* needle) {
        std::string result;
        while (result.find(needle) == std::string::npos) {
            PtyHandle::Chunk* const chunks = handle.acquire();
            STD_INSIST(chunks != nullptr);
            for (PtyHandle::Chunk* chunk = chunks; chunk != nullptr; chunk = chunk->next()) {
                result.append((const char*)(chunk->data()), chunk->length());
            }
            handle.release(chunks);
        }
        return result;
    }

    void sendAll(PtyHandle& handle, const void* data, size_t len) {
        const u8* bytes = (const u8*)(data);
        size_t remaining = len;
        while (remaining != 0) {
            PtyHandle::Chunk* const chunk = handle.allocate(remaining);
            const size_t count = chunk->length() < remaining ? chunk->length() : remaining;
            __builtin_memcpy(chunk->data(), bytes, count);
            handle.send(chunk, count);
            bytes += count;
            remaining -= count;
        }
    }

    // "<rows> <cols>\n", the only thing the helper's winsize modes print.
    bool parseWinsize(const std::string& text, unsigned& rows, unsigned& columns) {
        return sscanf(text.c_str(), "%u %u", &rows, &columns) == 2;
    }

    // Reaping by pid, never by -1: this binary forks in more than one
    // suite, and a blind wait hands the caller whichever corpse is ready -
    // a foreign one passes or fails the caller's checks by accident.
    int reapChild(pid_t child) {
        STD_INSIST(child > 0);
        int status = 0;
        STD_INSIST(waitpid(child, &status, 0) == child);
        return status;
    }
}

STD_TEST_SUITE(Pty) {
    STD_TEST(ChildOutputReachesEof) {
        RealPtyFixture fixture;
        ObjPool* const owner = ObjPool::fromMemoryRaw();
        char script[] = "printf pty-output";
        PtyHandle* const handle = spawnShell(*fixture.pty, *owner, script);
        const pid_t child = handle->childPid();

        const std::string output = readAll(*handle);
        delete owner;
        const int status = reapChild(child);

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
        // The production drain thread and its arena live until process exit.
        // Keep that contract here while the ordinary test arena still tears
        // down the sessions and their handles below.
        ObjPool* const ptyOwner = ObjPool::fromMemoryRaw();
        Pty* const real = createPty(*ptyOwner, *composer.platform->scheduler(), composer.platform);
        TwoSessionPty pty(composer, *real);
        composer.pty = &pty;
        composer.launch = &command;
        SessionSet* const sessions = SessionSet::create(composer);
        publish(composer.newTabListeners);
        publish(composer.prevTabListeners);
        const pid_t child = pty.doomed->childPid();

        // EOT makes the shell's canonical read return EOF, just like Ctrl+D.
        const u8 eot = 0x04;
        sendAll(*pty.doomed, &eot, 1);

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

        const int status = reapChild(child);
        STD_INSIST(WIFEXITED(status));
        STD_INSIST(WEXITSTATUS(status) == 0);
    }

    STD_TEST(InputRoundTripsThroughTheSlave) {
        RealPtyFixture fixture;
        ObjPool* const owner = ObjPool::fromMemoryRaw();
        char script[] = "stty -echo; IFS= read -r line; printf 'got:%s\\n' \"$line\"";
        PtyHandle* const handle = spawnShell(*fixture.pty, *owner, script);
        const pid_t child = handle->childPid();

        const char input[] = "hello from master\n";
        sendAll(*handle, input, sizeof(input) - 1);
        const std::string output = readAll(*handle);
        delete owner;
        const int status = reapChild(child);

        STD_INSIST(output.find("got:hello from master") != std::string::npos);
        STD_INSIST(WIFEXITED(status));
        STD_INSIST(WEXITSTATUS(status) == 0);
    }

    STD_TEST(LargeChildOutputSurvivesBackpressure) {
        RealPtyFixture fixture;
        ObjPool* const owner = ObjPool::fromMemoryRaw();
        char script[] = "head -c 1048576 /dev/zero";
        PtyHandle* const handle = spawnShell(*fixture.pty, *owner, script);
        const pid_t child = handle->childPid();

        // Let the child fill the finite slave-to-master queue before the
        // first read, then drain it through repeated readiness waits.
        usleep(50'000);
        size_t total = 0;
        size_t nonzero = 0;
        for (;;) {
            PtyHandle::Chunk* const chunks = handle->acquire();
            if (chunks == nullptr) {
                break;
            }
            for (PtyHandle::Chunk* chunk = chunks; chunk != nullptr; chunk = chunk->next()) {
                const u8* const bytes = (const u8*)(chunk->data());
                total += chunk->length();
                for (size_t index = 0; index < chunk->length(); ++index) {
                    nonzero += bytes[index] != 0;
                }
            }
            handle->release(chunks);
        }
        delete owner;
        const int status = reapChild(child);

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
        const pid_t child = handle->childPid();

        const std::string ready = readUntil(*handle, "\n");
        handle->resize({
            .columns = 123,
            .rows = 47,
            .pixelWidth = 984,
            .pixelHeight = 752,
        });
        const std::string output = readAll(*handle);
        delete owner;
        const int status = reapChild(child);

        STD_INSIST(ready.find("ready") != std::string::npos);
        STD_INSIST(output.find("47 123") != std::string::npos);
        STD_INSIST(WIFEXITED(status));
        STD_INSIST(WEXITSTATUS(status) == 0);
    }

    // The child reads TIOCGWINSZ as its first operation after exec, with
    // no SIGWINCH to wait for. Before the size was set on the slave ahead
    // of the fork, this answered "0 0" - the race ResizeReachesChildAsWinch
    // cannot see, because it prints ready before the resize it waits for.
    STD_TEST(TheChildIsBornWithTheSizeSpawnWasGiven) {
        RealPtyFixture fixture;
        ObjPool* const owner = ObjPool::fromMemoryRaw();
        char mode[] = "winsize-now";
        const PtySize born{
            .columns = 123,
            .rows = 47,
            .pixelWidth = 984,
            .pixelHeight = 752,
        };
        PtyHandle* const handle = spawnHelper(*fixture.pty, *owner, mode, born);
        const pid_t child = handle->childPid();

        const std::string output = readAll(*handle);
        delete owner;
        const int status = reapChild(child);

        unsigned rows = 0;
        unsigned columns = 0;
        STD_INSIST(parseWinsize(output, rows, columns));
        STD_INSIST(rows == 47);
        STD_INSIST(columns == 123);
        STD_INSIST(WIFEXITED(status));
        STD_INSIST(WEXITSTATUS(status) == 0);
    }

    // A8 end to end: the pane a split creates is told its geometry the
    // same way the first one is - at spawn, before the fork - so both
    // children can read it with their first operation. BornSizePty reads
    // that first operation inside spawn(), which is what makes the test
    // able to fail: applyLayout() resizes the newborn pane the moment
    // splitFocused() gets its session back, and until the observation
    // was moved ahead of it a slave sized only by that resize looked no
    // different here. Both children hold after reporting, so neither
    // pane closes and rewrites its sibling's geometry mid-test - a
    // second belt now rather than the load-bearing one, since the
    // reading is taken before anything drives the loop that would notice
    // a death (R1a-test round 2, finding 4).
    STD_TEST(EveryPanesChildIsBornWithThatPanesSize) {
        ObjPool::Ref pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Options options;
        // splitFocused() refuses while panes are off, as they are by default.
        options.panes = true;
        composer.opts = &options;
        VtermHeadless* const host = VtermHeadless::create(composer, nullptr);
        (void)(host);

        char program[] = "pty_ut";
        char execute[] = "-e";
        char* const helper = getenv("SHITTY_PTY_TEST_HELPER");
        STD_INSIST(helper != nullptr);
        char mode[] = "winsize-now-hold";
        char* argv[] = {program, execute, helper, mode, nullptr};
        const LaunchCommand command = buildLaunchCommand(4, argv, StringView(), false);

        // The production drain thread and its arena live until process exit.
        ObjPool* const ptyOwner = ObjPool::fromMemoryRaw();
        Pty* const real = createPty(*ptyOwner, *composer.platform->scheduler(), composer.platform);
        BornSizePty pty(*real);
        composer.pty = &pty;
        composer.launch = &command;
        // create() already opens the first session; a new tab on top of it
        // would be a third spawn this test has no slot for.
        SessionSet* const sessions = SessionSet::create(composer);
        STD_INSIST(sessions->splitFocused(SplitDirection::Vertical));
        STD_INSIST(pty.spawns == 2);

        // A vertical split halves the width and leaves the height alone.
        STD_INSIST(pty.born[0].rows != 0);
        STD_INSIST(pty.born[0].columns != 0);
        STD_INSIST(pty.born[1].rows == pty.born[0].rows);
        STD_INSIST(pty.born[1].columns != 0);
        STD_INSIST(pty.born[1].columns < pty.born[0].columns);

        // Each child is held to the size its own spawn() was given, both
        // axes. The first pane's child answered before the split existed
        // and the second one's before the layout pass that follows it,
        // so neither number can be one the parent set after the fork.
        unsigned rows = 0;
        unsigned columns = 0;
        STD_INSIST(parseWinsize(pty.heard[0], rows, columns));
        STD_INSIST(rows == pty.born[0].rows);
        STD_INSIST(columns == pty.born[0].columns);
        STD_INSIST(parseWinsize(pty.heard[1], rows, columns));
        STD_INSIST(rows == pty.born[1].rows);
        STD_INSIST(columns == pty.born[1].columns);
    }

    // The engaged path's hairy exit: the arena dies while the drain is
    // mid-flood and the feed holds acquired blocks. The destructor's
    // handshake must balance the ledger and hang up the child.
    STD_TEST(EngagedOwnerDeathSurvivesAFloodingChild) {
        // An engaged PTY starts the process-lifetime drain thread, so its
        // platform, scheduler and arena follow the production lifetime too.
        ObjPool* const pool = ObjPool::fromMemoryRaw();
        Composer& composer = *pool->make<Composer>(pool);
        VtermHeadless* const host = VtermHeadless::create(composer, nullptr);
        (void)(host);
        Pty* const pty = createPty(*composer.pool, *composer.platform->scheduler(), composer.platform);
        ObjPool* const owner = ObjPool::fromMemoryRaw();
        char mode[] = "flood-hangup";
        PtyHandle* const handle = spawnHelper(*pty, *owner, mode);
        const pid_t child = handle->childPid();
        handle->engage();

        size_t consumed = 0;
        auto feed = makeRunable([&] {
            for (;;) {
                PtyHandle::Chunk* const chunks = handle->acquire();
                if (chunks == nullptr) {
                    return;
                }
                for (PtyHandle::Chunk* chunk = chunks; chunk != nullptr; chunk = chunk->next()) {
                    consumed += chunk->chunk().length();
                }
                handle->release(chunks);
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
        // handle walks the two-phase goodbye with the drain. The helper
        // blocks SIGHUP while flooding and reports receiving it with zero.
        delete owner;
        const int status = reapChild(child);
        STD_INSIST(WIFEXITED(status));
        STD_INSIST(WEXITSTATUS(status) == 0);
    }

    STD_TEST(OwnerDeathReleasesBlockedIoAndHangsUpChild) {
        RealPtyFixture fixture;
        ObjPool* const owner = ObjPool::fromMemoryRaw();
        char mode[] = "hangup";
        PtyHandle* const handle = spawnHelper(*fixture.pty, *owner, mode);
        const pid_t child = handle->childPid();
        (void)(readUntil(*handle, "\n"));

        bool readerReturned = false;
        auto reader = makeRunable([&] {
            (void)!handle->acquire();
            readerReturned = true;
        });
        fixture.scheduler->create(*owner, reader);
        STD_INSIST(!readerReturned);

        std::string input(1024 * 1024, 'x');
        bool writerReturned = false;
        auto writer = makeRunable([&] {
            sendAll(*handle, input.data(), input.size());
            writerReturned = true;
        });
        fixture.scheduler->create(*owner, writer, 64 * 1024);
        STD_INSIST(!writerReturned);

        // LIFO pool teardown releases both client-owned fibers before the
        // handle closes the master and sends SIGHUP. A later poll round sees
        // only scheduler tombstones, never the freed stacks.
        delete owner;
        fixture.poller->wait(0);
        const int status = reapChild(child);

        STD_INSIST(!readerReturned);
        STD_INSIST(!writerReturned);
        STD_INSIST(WIFSIGNALED(status));
        STD_INSIST(WTERMSIG(status) == SIGHUP);
    }
}
