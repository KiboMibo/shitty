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

#include <plt/fiber.h>
#include <plt/platform.h>

#include <cstdio>

#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/thr/runable.h>

using namespace stl;

namespace {
    struct SessionSetImpl;

    // How often the reaper re-checks a grave that was not ready: the pty
    // is still draining its tail or a transaction fiber is in flight,
    // both matters of milliseconds.
    constexpr u64 gravePollUs = 10'000;

    // One node per terminal action, owned by the set and living as long as
    // it does. They never move, so an action cannot end up pointing at a
    // terminal that has stopped being the active one.
    struct CallSessionAction final: public Listener {
        CallSessionAction(SessionSetImpl* parent, InputActions action);

        void onListen(void*) override;

        SessionSetImpl* parent;
        InputActions action;
    };

    // The set is the one resize listener however many sessions there are:
    // a per-terminal registration could never leave composer's lists when
    // its session died, and background sessions still have to track the
    // window to be right when they come back.
    struct CallSessionsResize final: public Listener {
        explicit CallSessionsResize(SessionSetImpl* parent);

        void onListen(void*) override;

        SessionSetImpl* parent;
    };

    struct CallSessionsFontChanged final: public Listener {
        explicit CallSessionsFontChanged(SessionSetImpl* parent);

        void onListen(void*) override;

        SessionSetImpl* parent;
    };

    struct ReapBody final: public Runable {
        explicit ReapBody(SessionSetImpl* parent);

        void run() override;

        SessionSetImpl* parent;
    };

    // The window's single input handler. A terminal never joins the
    // router's chain: membership would then be what selects the active
    // one, which is exactly the coupling this removes.
    struct SessionSetImpl final: public SessionSet, public InputHandler {
        explicit SessionSetImpl(Composer& composer);

        size_t open(Pty* pty, VtermTraceFactory* traceFactory) override;
        size_t adopt(Vterm* terminal, Pty* pty) override;
        size_t count() const override;
        size_t active() const override;
        Vterm* activeTerminal() const override;
        Pty* ptyAt(size_t index) const override;
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

        void everyTerminalResized();
        void everyTerminalFontChanged();
        void runReaper();
        void reapReady();
        bool canReap(Vterm* terminal, Pty* pty) const;

        struct Session {
            Vterm* terminal = nullptr;
            Pty* pty = nullptr;
            // The session's own arena when open() built it, null for an
            // adopted pair. Everything the terminal is - the object, its
            // fiber stacks, its screens - dies when the arena does.
            stl::ObjPool* arena = nullptr;
        };

        struct Grave {
            stl::ObjPool* arena = nullptr;
            Vterm* terminal = nullptr;
            Pty* pty = nullptr;
        };

        Composer& composer;
        // Storage only: Vector has no erase and is not assignable, so
        // count_ is the live length and closing shifts the tail down.
        Vector<Session> sessions;
        size_t count_ = 0;
        size_t active_ = 0;
        // Closed sessions whose arena is not yet safe to drop; the reaper
        // fiber drains this. Same storage discipline as sessions.
        Vector<Grave> graves;
        size_t graveCount_ = 0;
        plt::Fiber* reaper_ = nullptr;
        // Replayed into a session as it becomes active, so a terminal that
        // was not there when the window gained focus still learns of it.
        bool focused_ = false;
        bool pointerPresent_ = false;
        CallSessionAction copyAction{this, InputActions::Copy};
        CallSessionAction pasteAction{this, InputActions::Paste};
        CallSessionAction pastePrimaryAction{this, InputActions::PastePrimary};
        CallSessionAction pageUpAction{this, InputActions::PageUp};
        CallSessionAction pageDownAction{this, InputActions::PageDown};
        CallSessionsResize resizeAction{this};
        CallSessionsFontChanged fontChangedAction{this};
        ReapBody reapBody{this};
        alignas(16) u8 reapStack[plt::lightFiberStack];
    };
}

CallSessionAction::CallSessionAction(SessionSetImpl* parent_, InputActions action_)
    : parent(parent_)
    , action(action_)
{
}

CallSessionsResize::CallSessionsResize(SessionSetImpl* parent_)
    : parent(parent_)
{
}

void CallSessionsResize::onListen(void*) {
    parent->everyTerminalResized();
}

CallSessionsFontChanged::CallSessionsFontChanged(SessionSetImpl* parent_)
    : parent(parent_)
{
}

void CallSessionsFontChanged::onListen(void*) {
    parent->everyTerminalFontChanged();
}

ReapBody::ReapBody(SessionSetImpl* parent_)
    : parent(parent_)
{
}

void ReapBody::run() {
    parent->runReaper();
}

SessionSetImpl::SessionSetImpl(Composer& composer_)
    : composer(composer_)
{
}

size_t SessionSetImpl::open(Pty* pty, VtermTraceFactory* traceFactory) {
    ObjPool* const arena = ObjPool::fromMemoryRaw();
    Vterm* terminal;
    try {
        terminal = Vterm::create(*arena, composer, traceFactory);
    } catch (...) {
        delete arena;
        throw;
    }
    // The pty feeds the terminal it was opened with, active or not; a
    // background shell's output must never parse into the foreground
    // screen.
    pty->bindTerminal(terminal);
    const size_t index = adopt(terminal, pty);
    sessions.mut(index).arena = arena;
    return index;
}

size_t SessionSetImpl::adopt(Vterm* terminal, Pty* pty) {
    if (count_ < sessions.length()) {
        sessions.mut(count_) = {terminal, pty, nullptr};
    } else {
        sessions.pushBack({terminal, pty, nullptr});
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
    // Unbound before the stop: the feed fiber drains its tail on a later
    // loop turn, and those bytes must go nowhere rather than into a
    // terminal already in its grave.
    sessions[index].pty->bindTerminal(nullptr);
    // The shell goes with its session. Without this the pty's threads,
    // its stacks and its master descriptor outlive every closed tab.
    sessions[index].pty->stop();
    const Grave grave{sessions[index].arena, sessions[index].terminal, sessions[index].pty};
    for (size_t at = index; at + 1 < count_; ++at) {
        sessions.mut(at) = sessions[at + 1];
    }
    --count_;
    SessionSet::liveSessions = (sig_atomic_t)(count_);
    if (count_ == 0) {
        // The window is closing with this last session; the renderer keeps
        // presenting the dead terminal until then, so its arena must not
        // drop. Process exit reclaims it.
        return false;
    }
    if (grave.arena != nullptr) {
        if (graveCount_ < graves.length()) {
            graves.mut(graveCount_) = grave;
        } else {
            graves.pushBack(grave);
        }
        ++graveCount_;
    }
    // The neighbour that shifted into this slot, or the new last one if
    // the tail went.
    activate(index < count_ ? index : count_ - 1);
    if (grave.arena != nullptr && reaper_ != nullptr) {
        reaper_->wake();
    }
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

void SessionSetImpl::everyTerminalResized() {
    // Background sessions track the window too: a terminal that resized
    // only on activation would replay its scrollback into wrong geometry.
    for (size_t at = 0; at < count_; ++at) {
        sessions[at].terminal->windowResized();
    }
}

void SessionSetImpl::everyTerminalFontChanged() {
    for (size_t at = 0; at < count_; ++at) {
        sessions[at].terminal->fontChanged();
    }
}

void SessionSetImpl::runReaper() {
    plt::Fiber* const self = composer.platform->scheduler()->current();
    reaper_ = self;
    for (;;) {
        if (graveCount_ == 0) {
            self->park();
            continue;
        }
        reapReady();
        if (graveCount_ != 0) {
            self->parkFor(gravePollUs);
        }
    }
}

void SessionSetImpl::reapReady() {
    size_t kept = 0;
    for (size_t at = 0; at < graveCount_; ++at) {
        const Grave grave = graves[at];
        if (canReap(grave.terminal, grave.pty)) {
            // Runs ~VtermImpl: the timer fibers are released off their
            // parked deadlines and the arena drops the whole terminal.
            delete grave.arena;
        } else {
            graves.mut(kept) = grave;
            ++kept;
        }
    }
    graveCount_ = kept;
}

bool SessionSetImpl::canReap(Vterm* terminal, Pty* pty) const {
    // The pty's feed fiber calls into the terminal until its tail drains,
    // and a transaction fiber holds the terminal across suspensions.
    if (!pty->drained() || !terminal->quiescent()) {
        return false;
    }
    // The renderer sheds the dead terminal's retained cells only when it
    // consumes the successor's full expose; until that frame lands, its
    // cell pointers still reach into the arena.
    if (composer.renderer != nullptr && activeTerminal()->presentationChanged()) {
        return false;
    }
    return true;
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
    composer.resizedListeners.pushBack(&sessions->resizeAction);
    composer.fontChangedListeners.pushBack(&sessions->fontChangedAction);
    composer.platform->scheduler()->spawn(sessions->reapBody, sessions->reapStack, sizeof(sessions->reapStack));
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
    // There is no "no sessions" state to represent: the window opens its
    // first session before its loop starts and dies the moment the last
    // one closes. Closing never shrinks the storage, so even the twilight
    // frames between that close and the exit still have their terminal.
    return sessions[active_].terminal;
}

Pty* SessionSetImpl::ptyAt(size_t index) const {
    return sessions[index].pty;
}

void CallSessionAction::onListen(void*) {
    Vterm* const terminal = parent->activeTerminal();
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
        default:
            break;
    }
}

bool SessionSetImpl::key(const plt::KeyInput& input) {
    return activeTerminal()->key(input);
}

bool SessionSetImpl::text(const plt::TextInput& input) {
    return activeTerminal()->text(input);
}

bool SessionSetImpl::pointerMotion(const plt::PointerMotionInput& input) {
    return activeTerminal()->pointerMotion(input);
}

bool SessionSetImpl::pointerButton(const plt::PointerButtonInput& input) {
    return activeTerminal()->pointerButton(input);
}

bool SessionSetImpl::scroll(const plt::ScrollInput& input) {
    return activeTerminal()->scroll(input);
}

void SessionSetImpl::focus(bool focused) {
    focused_ = focused;
    activeTerminal()->focus(focused);
}

void SessionSetImpl::pointerPresence(bool present) {
    pointerPresent_ = present;
    activeTerminal()->pointerPresence(present);
}

void SessionSetImpl::flush() {
    activeTerminal()->flush();
}
