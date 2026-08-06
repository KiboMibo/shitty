/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "session.h"

#include "composer.h"
#include "pty.h"
#include "vterm.h"
#include "vterm_headless.h"

#include <plt/fiber.h>
#include <plt/mutex.h>
#include <plt/platform.h>

#include <std/mem/obj_pool.h>
#include <std/tst/ut.h>

using namespace stl;

namespace {
    // A pty that accepts everything and goes nowhere: the tests here care
    // which pty a session selects, not what reaches a shell.
    struct StubPty final: public Pty {
        explicit StubPty(Composer& composer) {
            mutex_ = composer.platform->scheduler()->createMutex(*composer.pool);
        }

        Output* output() override {
            return nullptr;
        }

        plt::FiberMutex& mutex() override {
            return *mutex_;
        }

        size_t tryWrite(const u8*, size_t len) override {
            return len;
        }

        void stop() override {
            ++stops;
        }

        void bindTerminal(Vterm* terminal) override {
            bound = terminal;
            ++binds;
        }

        bool drained() const override {
            return true;
        }

        size_t stops = 0;
        Vterm* bound = nullptr;
        size_t binds = 0;

        plt::FiberMutex* mutex_ = nullptr;
    };
}

STD_TEST_SUITE(SessionSet) {
    // activeTerminal() is the one authoritative answer to which terminal
    // the window shows; activation must move it.
    STD_TEST(ActivateMakesTheSessionTheWindowsTerminal) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Vterm* const first = VtermHeadless::create(composer, nullptr)->terminal();
        Vterm* const second = Vterm::create(*composer.pool, composer, nullptr);
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
        Vterm* const second = Vterm::create(*composer.pool, composer, nullptr);
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
        Pty* const pty = composer.pty;
        Vterm* const second = Vterm::create(*composer.pool, composer, nullptr);
        Vterm* const third = Vterm::create(*composer.pool, composer, nullptr);
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
        Pty* const pty = composer.pty;
        Vterm* const second = Vterm::create(*composer.pool, composer, nullptr);
        Vterm* const third = Vterm::create(*composer.pool, composer, nullptr);
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

    // Closing a session must also end the shell behind it. Without this
    // the pty's threads, its stacks and its master descriptor outlive
    // every tab that is ever closed.
    STD_TEST(ClosingASessionStopsItsPty) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Vterm* const first = VtermHeadless::create(composer, nullptr)->terminal();
        Pty* const firstPty = composer.pty;
        StubPty doomed(composer);
        composer.pty = &doomed;
        Vterm* const second = Vterm::create(*composer.pool, composer, nullptr);
        SessionSet* const sessions = SessionSet::create(composer);
        sessions->adopt(first, firstPty);
        const size_t doomedIndex = sessions->adopt(second, &doomed);
        sessions->activate(doomedIndex);

        STD_INSIST(sessions->close(doomedIndex));

        STD_INSIST(doomed.stops == 1);
        STD_INSIST(sessions->count() == 1);
    }

    // open() owns the whole pairing: the terminal comes out of the
    // session's arena, the pty is bound to feed exactly that terminal,
    // and close() unbinds before the arena goes to its grave - with no
    // renderer and a drained pty the reaper drops it on the spot.
    STD_TEST(OpenBindsThePtyAndCloseReapsTheArena) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Vterm* const first = VtermHeadless::create(composer, nullptr)->terminal();
        SessionSet* const sessions = SessionSet::create(composer);
        sessions->adopt(first, composer.pty);
        StubPty pty(composer);
        // Vterm::create captures composer.pty as the terminal's own, so
        // the pty is published before open() builds the terminal.
        composer.pty = &pty;
        const size_t opened = sessions->open(&pty, nullptr);
        sessions->activate(opened);

        STD_INSIST(pty.binds == 1);
        STD_INSIST(pty.bound == sessions->activeTerminal());

        STD_INSIST(sessions->close(opened));

        STD_INSIST(pty.binds == 2);
        STD_INSIST(pty.bound == nullptr);
        STD_INSIST(pty.stops == 1);
        STD_INSIST(sessions->count() == 1);
    }

    // A pty the set never adopted closes nothing; the shell EOF path may
    // race a close that already removed its session.
    STD_TEST(ClosingByAStrangerPtyTouchesNothing) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Vterm* const first = VtermHeadless::create(composer, nullptr)->terminal();
        Pty* const firstPty = composer.pty;
        Vterm* const second = Vterm::create(*composer.pool, composer, nullptr);
        SessionSet* const sessions = SessionSet::create(composer);
        sessions->adopt(first, firstPty);
        sessions->adopt(second, firstPty);
        StubPty stranger(composer);

        STD_INSIST(sessions->closeByPty(&stranger));

        STD_INSIST(sessions->count() == 2);
        STD_INSIST(stranger.stops == 0);
    }

    // A session is a terminal and the shell behind it. Activating must
    // move composer.pty too, or the window would show one session's
    // screen while resize and every other composer.pty reader addressed
    // another session's shell.
    STD_TEST(ActivateSelectsTheSessionsPty) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Vterm* const first = VtermHeadless::create(composer, nullptr)->terminal();
        Pty* const firstPty = composer.pty;
        StubPty secondPty(composer);
        composer.pty = &secondPty;
        Vterm* const second = Vterm::create(*composer.pool, composer, nullptr);
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
