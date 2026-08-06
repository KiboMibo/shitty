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
#include "terminal_types.h"
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
        void publishCount();

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
    publishCount();
    return count_ - 1;
}

bool SessionSetImpl::close(size_t index) {
    if (index >= count_) {
        return count_ != 0;
    }
    // The terminal leaves the input chain before its slot is reused, or
    // the chain keeps a node pointing at a record that has moved.
    sessions[index].terminal->deactivate();
    // The shell goes with its session. Without this the pty's threads,
    // its stacks and its master descriptor outlive every closed tab.
    sessions[index].pty->stop();
    for (size_t at = index; at + 1 < count_; ++at) {
        sessions.mut(at) = sessions[at + 1];
    }
    --count_;
    publishCount();
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
        fprintf(stderr, "%s: session: activated %zu of %zu\n", composer.brand->identifierCString(), index + 1, count_);
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

// The band exists exactly while more than one session does. This lives
// here rather than in the tab commands because a session can also go
// through the pty's EOF path, which never passes through Application.
void SessionSetImpl::publishCount() {
    SessionSet::liveSessions = (sig_atomic_t)(count_);
    // One text row of chrome. Zero with a single session, so a window that
    // never opens a tab keeps exactly the geometry it always had.
    composer.setTopInset(count_ > 1 ? composer.glyphHeight : 0);
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

bool buildTabBarRow(Composer& composer, TerminalCell* cells, u16 columns) {
    if (composer.sessions == nullptr || columns == 0) {
        return false;
    }
    const size_t sessions = composer.sessions->count();
    if (sessions < 2) {
        return false;
    }
    const size_t active = composer.sessions->active();
    const u16 segment = (u16)(columns / sessions);
    for (u16 column = 0; column < columns; ++column) {
        size_t which = segment != 0 ? (size_t)(column / segment) : 0;
        if (which >= sessions) {
            // Integer division leaves a remainder; the last segment takes it.
            which = sessions - 1;
        }
        // The number one cell in from the segment's edge, as many digits
        // as it takes, and only when the segment has room for it plus a
        // cell of padding either side.
        u32 number = (u32)(which + 1);
        u16 digits = 1;
        for (u32 rest = number; rest >= 10; rest /= 10) {
            ++digits;
        }
        const u16 start = segment != 0 ? (u16)(which * segment) : 0;
        u32 codepoint = ' ';
        if (segment >= digits + 2 && column > start && column <= start + digits) {
            u16 place = (u16)(start + digits - column);
            for (u16 step = 0; step < place; ++step) {
                number /= 10;
            }
            codepoint = '0' + (number % 10);
        }
        TerminalCell cell{};
        cell.uc_pt = codepoint;
        const bool here = which == active;
        cell.setForeground(here ? CellColor::defaultBackground() : CellColor::defaultForeground());
        cell.setBackground(here ? CellColor::defaultForeground() : CellColor::defaultBackground());
        cells[column] = cell;
    }
    return true;
}
