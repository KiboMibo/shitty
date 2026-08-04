/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "session.h"

#include "composer.h"
#include "vterm.h"
#include "vterm_headless.h"

#include <std/mem/obj_pool.h>
#include <std/tst/ut.h>

using namespace stl;

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
        const size_t firstIndex = sessions->adopt(first);
        const size_t secondIndex = sessions->adopt(second);

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
        sessions->adopt(first);
        const size_t secondIndex = sessions->adopt(second);

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
}
