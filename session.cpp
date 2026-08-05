/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "session.h"

#include "brand.h"
#include "composer.h"
#include "options.h"
#include "pty.h"
#include "vterm.h"

#include <cstdio>

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
        bool activateNext() override;
        bool activatePrevious() override;
        bool close(size_t index) override;
        bool closeByPty(Pty* pty) override;
        bool closeActive() override;

        struct Session {
            Vterm* terminal = nullptr;
            Pty* pty = nullptr;
        };

        Composer& composer;
        // Storage only: Vector has no erase and is not assignable, so
        // count_ is the live length and closing shifts the tail down.
        Vector<Session> sessions;
        size_t count_ = 0;
        size_t active_ = 0;
    };
}

size_t SessionSetImpl::adopt(Vterm* terminal, Pty* pty) {
    if (count_ < sessions.length()) {
        sessions.mut(count_) = {terminal, pty};
    } else {
        sessions.pushBack({terminal, pty});
    }
    ++count_;
    SessionSet::liveSessions = (sig_atomic_t)(count_);
    return count_ - 1;
}

bool SessionSetImpl::close(size_t index) {
    if (index >= count_) {
        return count_ != 0;
    }
    // The terminal leaves the input chain before its slot is reused, or
    // the chain keeps a node pointing at a record that has moved.
    sessions[index].terminal->deactivate();
    for (size_t at = index; at + 1 < count_; ++at) {
        sessions.mut(at) = sessions[at + 1];
    }
    --count_;
    SessionSet::liveSessions = (sig_atomic_t)(count_);
    if (count_ == 0) {
        return false;
    }
    // The neighbour that shifted into this slot, or the new last one if
    // the tail went.
    activate(index < count_ ? index : count_ - 1);
    return true;
}

size_t SessionSetImpl::count() const {
    return count_;
}

size_t SessionSetImpl::active() const {
    return active_;
}

void SessionSetImpl::activate(size_t index) {
    if (index >= count_) {
        return;
    }
    // Every terminal leaves the input chain first, including the incoming
    // one: Vterm::create puts each terminal on the chain as it is built,
    // so before the first activation more than one is on it.
    for (size_t at = 0; at < count_; ++at) {
        sessions[at].terminal->deactivate();
    }
    active_ = index;
    // The pty moves with the terminal. Everything that still reads
    // composer.pty - resize, the window title, the test harness - has to
    // address the shell whose screen the window is showing.
    composer.pty = sessions[index].pty;
    sessions[index].terminal->activate();
    if (composer.opts->verbose) {
        fprintf(stderr, "%s: session: activated %zu of %zu\n", composer.brand->identifierCString(), index + 1, sessions.length());
    }
}

bool SessionSetImpl::closeByPty(Pty* pty) {
    for (size_t at = 0; at < count_; ++at) {
        if (sessions[at].pty == pty) {
            return close(at);
        }
    }
    return count_ != 0;
}

bool SessionSetImpl::closeActive() {
    return close(active_);
}

volatile sig_atomic_t SessionSet::liveSessions = 0;

SessionSet* SessionSet::create(Composer& composer) {
    return composer.pool->make<SessionSetImpl>(composer);
}

bool SessionSetImpl::activateNext() {
    if (count_ < 2) {
        return false;
    }
    activate((active_ + 1) % count_);
    return true;
}

bool SessionSetImpl::activatePrevious() {
    if (count_ < 2) {
        return false;
    }
    activate(active_ == 0 ? count_ - 1 : active_ - 1);
    return true;
}
