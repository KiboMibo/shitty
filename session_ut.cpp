/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "session.h"

#include "composer.h"
#include "pty.h"
#include "startup.h"
#include "vterm.h"
#include "vterm_headless.h"

#include <plt/fiber.h>
#include <plt/platform.h>

#include <std/ios/input.h>
#include <std/ios/output.h>
#include <std/mem/obj_pool.h>
#include <std/tst/ut.h>

using namespace stl;

namespace {
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

    struct ParkOutput final: public Output {
        ParkOutput(plt::Scheduler& scheduler_, bool* entered_, bool* resumed_)
            : scheduler(scheduler_)
            , entered(entered_)
            , resumed(resumed_)
        {
        }

        size_t writeImpl(const void*, size_t size) override {
            if (entered == nullptr) {
                return size;
            }
            *entered = true;
            scheduler.current()->park();
            *resumed = true;
            return size;
        }

        plt::Scheduler& scheduler;
        bool* entered;
        bool* resumed;
    };

    // A pool-owned handle that accepts everything into the headless null
    // sink and keeps its reader parked until its arena is destroyed.
    struct StubHandle final: public PtyHandle {
        StubHandle(Composer& composer_, size_t* destroyed_ = nullptr, bool* writeEntered = nullptr, bool* writeResumed = nullptr)
            : composer(composer_)
            , destroyed(destroyed_)
            , input_(*composer.platform->scheduler())
            , parkedOutput_(*composer.platform->scheduler(), writeEntered, writeResumed)
            , output_(writeEntered != nullptr ? static_cast<Output*>(&parkedOutput_) : composer.ptyOutput)
        {
        }

        ~StubHandle() noexcept {
            if (destroyed != nullptr) {
                ++*destroyed;
            }
        }

        Input* input() override {
            return &input_;
        }

        Output* output() override {
            return output_;
        }

        void resize(const PtySize& requested) override {
            size = requested;
            ++resizes;
        }

        Composer& composer;
        size_t* destroyed;
        ParkInput input_;
        ParkOutput parkedOutput_;
        Output* output_;
        PtySize size{};
        size_t resizes = 0;
    };

    struct StubPty final: public Pty {
        explicit StubPty(Composer& composer_)
            : composer(composer_)
        {
        }

        PtyHandle* spawn(ObjPool& owner, const LaunchCommand&) override {
            last = owner.make<StubHandle>(composer, &destroyed, blockWrites ? &writeEntered : nullptr, blockWrites ? &writeResumed : nullptr);
            return last;
        }

        Composer& composer;
        StubHandle* last = nullptr;
        size_t destroyed = 0;
        bool blockWrites = false;
        bool writeEntered = false;
        bool writeResumed = false;
    };
}

STD_TEST_SUITE(SessionSet) {
    // activeTerminal() is the one authoritative answer to which terminal
    // the window shows; activation must move it.
    STD_TEST(ActivateMakesTheSessionTheWindowsTerminal) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Vterm* const first = VtermHeadless::create(composer, nullptr)->terminal();
        Vterm* const second = Vterm::create(*composer.pool, composer, *composer.pty->output(), nullptr);
        SessionSet* const sessions = SessionSet::create(composer);
        const size_t firstIndex = sessions->adopt(first, composer.pty);
        const size_t secondIndex = sessions->adopt(second, composer.pty);

        sessions->activate(firstIndex);

        STD_INSIST(sessions->count() == 2);
        STD_INSIST(sessions->active() == firstIndex);
        STD_INSIST(sessions->activeTerminal() == first);

        sessions->activate(secondIndex);

        STD_INSIST(sessions->active() == secondIndex);
        STD_INSIST(sessions->activeTerminal() == second);
    }

    // No terminal is on the router's chain at all: the set is the one
    // handler and dispatches to whichever session is active. Membership
    // used to be what selected a terminal, which meant a background one
    // left on the chain swallowed every keystroke meant for the active
    // one. It cannot now, because a Vterm is not an InputHandler.
    STD_TEST(NoTerminalJoinsTheInputChain) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Vterm* const first = VtermHeadless::create(composer, nullptr)->terminal();
        Vterm* const second = Vterm::create(*composer.pool, composer, *composer.pty->output(), nullptr);
        SessionSet* const sessions = SessionSet::create(composer);
        sessions->adopt(first, composer.pty);
        const size_t secondIndex = sessions->adopt(second, composer.pty);

        sessions->activate(secondIndex);

        // InputBindings and the session set. Two, whatever the session
        // count: opening terminals never lengthens this chain.
        size_t handlers = 0;
        for (IntrusiveNode* node = composer.inputHandlers.mutFront(); node != composer.inputHandlers.mutEnd(); node = node->next) {
            ++handlers;
        }
        STD_INSIST(handlers == 2);
        STD_INSIST(sessions->activeTerminal() == second);
    }

    // Switching wraps in both directions: from the last session forward
    // lands on the first, and from the first backward lands on the last.
    STD_TEST(NextAndPreviousWrapAround) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Vterm* const first = VtermHeadless::create(composer, nullptr)->terminal();
        PtyHandle* const pty = composer.pty;
        Vterm* const second = Vterm::create(*composer.pool, composer, *pty->output(), nullptr);
        Vterm* const third = Vterm::create(*composer.pool, composer, *pty->output(), nullptr);
        SessionSet* const sessions = SessionSet::create(composer);
        sessions->adopt(first, pty);
        sessions->adopt(second, pty);
        sessions->adopt(third, pty);
        sessions->activate(0);

        sessions->activateNext();
        STD_INSIST(sessions->active() == 1);
        sessions->activateNext();
        STD_INSIST(sessions->active() == 2);
        sessions->activateNext();
        STD_INSIST(sessions->active() == 0);

        sessions->activatePrevious();
        STD_INSIST(sessions->active() == 2);
        sessions->activatePrevious();
        STD_INSIST(sessions->active() == 1);
    }

    // One session is the common case and both directions must be a no-op
    // rather than a needless full-grid repaint.
    STD_TEST(SwitchingOneSessionStaysPut) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Vterm* const only = VtermHeadless::create(composer, nullptr)->terminal();
        SessionSet* const sessions = SessionSet::create(composer);
        sessions->adopt(only, composer.pty);
        sessions->activate(0);

        STD_INSIST(!sessions->activateNext());
        STD_INSIST(!sessions->activatePrevious());
        STD_INSIST(sessions->active() == 0);
    }

    // A shell exiting must take its own session and nothing else. The
    // neighbour becomes active so the window always shows something.
    STD_TEST(ClosingASessionKeepsTheOthers) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Vterm* const first = VtermHeadless::create(composer, nullptr)->terminal();
        PtyHandle* const pty = composer.pty;
        Vterm* const second = Vterm::create(*composer.pool, composer, *pty->output(), nullptr);
        Vterm* const third = Vterm::create(*composer.pool, composer, *pty->output(), nullptr);
        SessionSet* const sessions = SessionSet::create(composer);
        sessions->adopt(first, pty);
        sessions->adopt(second, pty);
        sessions->adopt(third, pty);
        sessions->activate(1);

        STD_INSIST(sessions->close(1));
        STD_INSIST(sessions->count() == 2);
        STD_INSIST(sessions->activeTerminal() == third);

        STD_INSIST(sessions->close(0));
        STD_INSIST(sessions->count() == 1);
        STD_INSIST(sessions->activeTerminal() == third);
    }

    // The last session closing is the window closing: close() reports it
    // so the caller can shut the window down instead of leaving it empty.
    STD_TEST(ClosingTheLastSessionReportsEmpty) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Vterm* const only = VtermHeadless::create(composer, nullptr)->terminal();
        SessionSet* const sessions = SessionSet::create(composer);
        sessions->adopt(only, composer.pty);
        sessions->activate(0);

        STD_INSIST(!sessions->close(0));
        STD_INSIST(sessions->count() == 0);
        // The slot outlives the close: the window presents this terminal
        // for its remaining twilight frames.
        STD_INSIST(sessions->activeTerminal() == only);
    }

    // Closing an owned session drops its arena, which is the only lifetime
    // signal the handle and its parked reader need.
    STD_TEST(ClosingASessionDestroysItsHandleArena) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Vterm* const first = VtermHeadless::create(composer, nullptr)->terminal();
        PtyHandle* const firstPty = composer.pty;
        SessionSet* const sessions = SessionSet::create(composer);
        sessions->adopt(first, firstPty);
        StubPty doomed(composer);
        const LaunchCommand command;
        const size_t doomedIndex = sessions->open(doomed, command, nullptr);
        sessions->activate(doomedIndex);

        STD_INSIST(sessions->close(doomedIndex));

        STD_INSIST(doomed.destroyed == 1);
        STD_INSIST(sessions->count() == 1);
    }

    // open() creates the handle, terminal and client reader in the same
    // arena, and gives the handle the current terminal geometry.
    STD_TEST(OpenSpawnsAndSizesTheHandle) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Vterm* const first = VtermHeadless::create(composer, nullptr)->terminal();
        SessionSet* const sessions = SessionSet::create(composer);
        sessions->adopt(first, composer.pty);
        StubPty pty(composer);
        const LaunchCommand command;
        const size_t opened = sessions->open(pty, command, nullptr);
        sessions->activate(opened);

        STD_INSIST(pty.last != nullptr);
        STD_INSIST(pty.last->resizes == 1);
        STD_INSIST(sessions->handleAt(opened) == pty.last);

        STD_INSIST(sessions->close(opened));

        STD_INSIST(pty.destroyed == 1);
        STD_INSIST(sessions->count() == 1);
    }

    STD_TEST(ClosingReleasesAParkedClientWriteFiber) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Vterm* const first = VtermHeadless::create(composer, nullptr)->terminal();
        SessionSet* const sessions = SessionSet::create(composer);
        sessions->adopt(first, composer.pty);
        StubPty pty(composer);
        pty.blockWrites = true;
        const LaunchCommand command;
        const size_t opened = sessions->open(pty, command, nullptr);
        sessions->activate(opened);

        sessions->activeTerminal()->sendBytes(StringView(u8"x"), true);
        STD_INSIST(pty.writeEntered);
        STD_INSIST(!pty.writeResumed);

        STD_INSIST(sessions->close(opened));

        STD_INSIST(pty.destroyed == 1);
        STD_INSIST(!pty.writeResumed);
    }

    STD_TEST(HandleAtReturnsTheSessionsHandle) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Vterm* const first = VtermHeadless::create(composer, nullptr)->terminal();
        PtyHandle* const firstPty = composer.pty;
        StubHandle secondPty(composer);
        Vterm* const second = Vterm::create(*composer.pool, composer, *secondPty.output(), nullptr);
        SessionSet* const sessions = SessionSet::create(composer);
        sessions->adopt(first, firstPty);
        const size_t secondIndex = sessions->adopt(second, &secondPty);

        STD_INSIST(sessions->handleAt(0) == firstPty);
        STD_INSIST(sessions->handleAt(secondIndex) == &secondPty);
    }

    // A session is a terminal and the shell behind it. Activating must
    // move composer.pty too, or the window would show one session's
    // screen while resize and every other composer.pty reader addressed
    // another session's shell.
    STD_TEST(ActivateSelectsTheSessionsPty) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Vterm* const first = VtermHeadless::create(composer, nullptr)->terminal();
        PtyHandle* const firstPty = composer.pty;
        StubHandle secondPty(composer);
        Vterm* const second = Vterm::create(*composer.pool, composer, *secondPty.output(), nullptr);
        SessionSet* const sessions = SessionSet::create(composer);
        const size_t firstIndex = sessions->adopt(first, firstPty);
        const size_t secondIndex = sessions->adopt(second, &secondPty);

        sessions->activate(firstIndex);

        STD_INSIST(sessions->activeTerminal() == first);
        STD_INSIST(composer.pty == firstPty);

        sessions->activate(secondIndex);

        STD_INSIST(sessions->activeTerminal() == second);
        STD_INSIST(composer.pty == &secondPty);
    }
}
