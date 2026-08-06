/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "session.h"

#include "brand.h"
#include "composer.h"
#include "input_bindings.h"
#include "input_handler.h"
#include "listener.h"
#include "options.h"
#include "pty.h"
#include "vterm.h"

#include <cstdio>

#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    struct SessionSetImpl;

    // One node per terminal action, owned by the set and living as long as
    // it does. They never move, so an action cannot end up pointing at a
    // terminal that has stopped being the active one.
    struct CallSessionAction final: public Listener {
        CallSessionAction(SessionSetImpl* parent_, InputActions action_)
            : parent(parent_)
            , action(action_)
        {
        }

        void onListen(void*) override;

        SessionSetImpl* parent;
        InputActions action;
    };

    // The window's single input handler. A terminal never joins the
    // router's chain: membership would then be what selects the active
    // one, which is exactly the coupling this removes.
    struct SessionSetImpl final: public SessionSet, public InputHandler {
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

        bool key(const plt::KeyInput& input) override;
        bool text(const plt::TextInput& input) override;
        bool pointerMotion(const plt::PointerMotionInput& input) override;
        bool pointerButton(const plt::PointerButtonInput& input) override;
        bool scroll(const plt::ScrollInput& input) override;
        void focus(bool focused) override;
        void pointerPresence(bool present) override;
        void flush() override;

        void publishCount();
        Vterm* activeTerminal() const;

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
        // Replayed into a session as it becomes active, so a terminal that
        // was not there when the window gained focus still learns of it.
        bool focused_ = false;
        bool pointerPresent_ = false;
        CallSessionAction copyAction{this, InputActions::Copy};
        CallSessionAction pasteAction{this, InputActions::Paste};
        CallSessionAction pastePrimaryAction{this, InputActions::PastePrimary};
        CallSessionAction pageUpAction{this, InputActions::PageUp};
        CallSessionAction pageDownAction{this, InputActions::PageDown};
        CallSessionAction clearAction{this, InputActions::Clear};
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
    // The shell goes with its session. Without this the pty's threads,
    // its stacks and its master descriptor outlive every closed tab.
    sessions[index].pty->stop();
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
    // The window's state, replayed. A session that was not there when the
    // window gained focus or the pointer arrived still has to hear it.
    sessions[index].terminal->focus(focused_);
    sessions[index].terminal->pointerPresence(pointerPresent_);
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

volatile sig_atomic_t SessionSet::liveSessions = 0;

SessionSet* SessionSet::create(Composer& composer) {
    SessionSetImpl* const sessions = composer.pool->make<SessionSetImpl>(composer);
    // Behind InputBindings, which must keep first refusal on the chords.
    composer.inputHandlers.pushBack(sessions);
    composer.copyListeners.pushBack(&sessions->copyAction);
    composer.pasteListeners.pushBack(&sessions->pasteAction);
    composer.pastePrimaryListeners.pushBack(&sessions->pastePrimaryAction);
    composer.pageUpListeners.pushBack(&sessions->pageUpAction);
    composer.pageDownListeners.pushBack(&sessions->pageDownAction);
    composer.clearListeners.pushBack(&sessions->clearAction);
    return sessions;
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

Vterm* SessionSetImpl::activeTerminal() const {
    return count_ != 0 ? sessions[active_].terminal : nullptr;
}

void CallSessionAction::onListen(void*) {
    Vterm* const terminal = parent->activeTerminal();
    if (terminal == nullptr) {
        return;
    }
    switch (action) {
        case InputActions::Copy:
            terminal->copy();
            break;
        case InputActions::Paste:
            terminal->paste(false);
            break;
        case InputActions::PastePrimary:
            terminal->paste(true);
            break;
        case InputActions::PageUp:
            terminal->pageUp();
            break;
        case InputActions::PageDown:
            terminal->pageDown();
            break;
        case InputActions::Clear:
            terminal->clear();
            break;
        default:
            break;
    }
}

bool SessionSetImpl::key(const plt::KeyInput& input) {
    Vterm* const terminal = activeTerminal();
    return terminal != nullptr && terminal->key(input);
}

bool SessionSetImpl::text(const plt::TextInput& input) {
    Vterm* const terminal = activeTerminal();
    return terminal != nullptr && terminal->text(input);
}

bool SessionSetImpl::pointerMotion(const plt::PointerMotionInput& input) {
    Vterm* const terminal = activeTerminal();
    return terminal != nullptr && terminal->pointerMotion(input);
}

bool SessionSetImpl::pointerButton(const plt::PointerButtonInput& input) {
    Vterm* const terminal = activeTerminal();
    return terminal != nullptr && terminal->pointerButton(input);
}

bool SessionSetImpl::scroll(const plt::ScrollInput& input) {
    Vterm* const terminal = activeTerminal();
    return terminal != nullptr && terminal->scroll(input);
}

void SessionSetImpl::focus(bool focused) {
    focused_ = focused;
    if (Vterm* const terminal = activeTerminal()) {
        terminal->focus(focused);
    }
}

void SessionSetImpl::pointerPresence(bool present) {
    pointerPresent_ = present;
    if (Vterm* const terminal = activeTerminal()) {
        terminal->pointerPresence(present);
    }
}

void SessionSetImpl::flush() {
    if (Vterm* const terminal = activeTerminal()) {
        terminal->flush();
    }
}
