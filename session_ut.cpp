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

#include <plt/mutex.h>

#include <std/mem/obj_pool.h>
#include <std/tst/ut.h>

using namespace stl;

namespace {
    // A pty that accepts everything and goes nowhere: the tests here care
    // which pty a session selects, not what reaches a shell.
    struct StubPty final: public Pty {
        Output* output() override {
            return nullptr;
        }

        plt::FiberMutex& mutex() override {
            return mutex_;
        }

        size_t tryWrite(const u8*, size_t len) override {
            return len;
        }

        plt::FiberMutex mutex_;
    };
}

STD_TEST_SUITE(SessionSet) {
    // Vterm::create leaves the terminal it just built as composer.vterm,
    // so activating an earlier session has to move it back.
    STD_TEST(ActivateMakesTheSessionTheWindowsTerminal) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        VtermHeadless::create(composer, nullptr);
        Vterm* const first = composer.vterm;
        Vterm* const second = Vterm::create(composer, nullptr);
        SessionSet* const sessions = SessionSet::create(composer);
        const size_t firstIndex = sessions->adopt(first, composer.pty);
        const size_t secondIndex = sessions->adopt(second, composer.pty);

        sessions->activate(firstIndex);

        STD_INSIST(sessions->count() == 2);
        STD_INSIST(sessions->active() == firstIndex);
        STD_INSIST(composer.vterm == first);

        sessions->activate(secondIndex);

        STD_INSIST(sessions->active() == secondIndex);
        STD_INSIST(composer.vterm == second);
    }

    // inputHandlers is a first-accepts-wins chain and VtermInput::key
    // accepts everything, so a background terminal left on the list would
    // swallow every keystroke meant for the active one. Exactly one
    // terminal may be on the chain at a time.
    STD_TEST(OnlyTheActiveTerminalIsOnTheInputChain) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        VtermHeadless::create(composer, nullptr);
        Vterm* const first = composer.vterm;
        Vterm* const second = Vterm::create(composer, nullptr);
        SessionSet* const sessions = SessionSet::create(composer);
        sessions->adopt(first, composer.pty);
        const size_t secondIndex = sessions->adopt(second, composer.pty);

        sessions->activate(secondIndex);

        // InputBindings plus exactly one terminal. Vterm does not derive
        // from InputHandler - VtermImpl inherits the two separately - so
        // the chain is counted rather than searched for a terminal.
        size_t handlers = 0;
        for (IntrusiveNode* node = composer.inputHandlers.mutFront(); node != composer.inputHandlers.mutEnd(); node = node->next) {
            ++handlers;
        }
        STD_INSIST(handlers == 2);
        STD_INSIST(composer.vterm == second);
    }

    // Switching wraps in both directions: from the last session forward
    // lands on the first, and from the first backward lands on the last.
    STD_TEST(NextAndPreviousWrapAround) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        VtermHeadless::create(composer, nullptr);
        Vterm* const first = composer.vterm;
        Pty* const pty = composer.pty;
        Vterm* const second = Vterm::create(composer, nullptr);
        Vterm* const third = Vterm::create(composer, nullptr);
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
        VtermHeadless::create(composer, nullptr);
        SessionSet* const sessions = SessionSet::create(composer);
        sessions->adopt(composer.vterm, composer.pty);
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
        VtermHeadless::create(composer, nullptr);
        Vterm* const first = composer.vterm;
        Pty* const pty = composer.pty;
        Vterm* const second = Vterm::create(composer, nullptr);
        Vterm* const third = Vterm::create(composer, nullptr);
        SessionSet* const sessions = SessionSet::create(composer);
        sessions->adopt(first, pty);
        sessions->adopt(second, pty);
        sessions->adopt(third, pty);
        sessions->activate(1);

        STD_INSIST(sessions->close(1));
        STD_INSIST(sessions->count() == 2);
        STD_INSIST(composer.vterm == third);

        STD_INSIST(sessions->close(0));
        STD_INSIST(sessions->count() == 1);
        STD_INSIST(composer.vterm == third);
    }

    // The last session closing is the window closing: close() reports it
    // so the caller can shut the window down instead of leaving it empty.
    STD_TEST(ClosingTheLastSessionReportsEmpty) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        VtermHeadless::create(composer, nullptr);
        SessionSet* const sessions = SessionSet::create(composer);
        sessions->adopt(composer.vterm, composer.pty);
        sessions->activate(0);

        STD_INSIST(!sessions->close(0));
        STD_INSIST(sessions->count() == 0);
    }

    // A session is a terminal and the shell behind it. Activating must
    // move composer.pty too, or the window would show one session's
    // screen while resize and every other composer.pty reader addressed
    // another session's shell.
    STD_TEST(ActivateSelectsTheSessionsPty) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        VtermHeadless::create(composer, nullptr);
        Vterm* const first = composer.vterm;
        Pty* const firstPty = composer.pty;
        StubPty secondPty;
        composer.pty = &secondPty;
        Vterm* const second = Vterm::create(composer, nullptr);
        SessionSet* const sessions = SessionSet::create(composer);
        const size_t firstIndex = sessions->adopt(first, firstPty);
        const size_t secondIndex = sessions->adopt(second, &secondPty);

        sessions->activate(firstIndex);

        STD_INSIST(composer.vterm == first);
        STD_INSIST(composer.pty == firstPty);

        sessions->activate(secondIndex);

        STD_INSIST(composer.vterm == second);
        STD_INSIST(composer.pty == &secondPty);
    }
}
