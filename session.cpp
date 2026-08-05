/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "session.h"

#include "composer.h"
#include "pty.h"
#include "vterm.h"

#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    struct SessionSetImpl final: public SessionSet {
        explicit SessionSetImpl(Composer& composer_)
            : composer(composer_)
        {
        }

        size_t adopt(Vterm* terminal, Pty* pty) override;
        size_t count() const override;
        size_t active() const override;
        void activate(size_t index) override;

        struct Session {
            Vterm* terminal = nullptr;
            Pty* pty = nullptr;
        };

        Composer& composer;
        Vector<Session> sessions;
        size_t active_ = 0;
    };
}

size_t SessionSetImpl::adopt(Vterm* terminal, Pty* pty) {
    sessions.pushBack({terminal, pty});
    return sessions.length() - 1;
}

size_t SessionSetImpl::count() const {
    return sessions.length();
}

size_t SessionSetImpl::active() const {
    return active_;
}

void SessionSetImpl::activate(size_t index) {
    if (index >= sessions.length()) {
        return;
    }
    // Every terminal leaves the input chain first, including the incoming
    // one: Vterm::create puts each terminal on the chain as it is built,
    // so before the first activation more than one is on it.
    for (size_t at = 0; at < sessions.length(); ++at) {
        sessions[at].terminal->deactivate();
    }
    active_ = index;
    // The pty moves with the terminal. Everything that still reads
    // composer.pty - resize, the window title, the test harness - has to
    // address the shell whose screen the window is showing.
    composer.pty = sessions[index].pty;
    sessions[index].terminal->activate();
}

SessionSet* SessionSet::create(Composer& composer) {
    return composer.pool->make<SessionSetImpl>(composer);
}
