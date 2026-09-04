/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "session.h"

#include "pty.h"
#include "brand.h"
#include "options.h"
#include "composer.h"
#include "pane_layout.h"
#include "input_bindings.h"

#include <lib/vterm/vterm.h>
#include <lib/vterm/listener.h>
#include <lib/vterm/input_handler.h>
#include <lib/vterm/cell_extra_store.h>

#include <std/alg/minmax.h>
#include <std/lib/buffer.h>
#include <std/lib/vector.h>
#include <std/thr/runable.h>
#include <std/mem/obj_pool.h>

#include <stdio.h>
#include <plt/fiber.h>
#include <plt/poller.h>
#include <plt/window.h>
#include <plt/platform.h>
#include <plt/loop_wake.h>

using namespace stl;

namespace {
    struct SessionSetImpl;

    // How often the reaper re-checks a grave whose cells are still retained
    // by the renderer while the successor's full expose is in flight.
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

    struct CallSessionsConfigChanged final: public Listener {
        explicit CallSessionsConfigChanged(SessionSetImpl* parent);

        void onListen(void*) override;

        SessionSetImpl* parent;
    };

    struct CallTitleChanged final: public Listener {
        explicit CallTitleChanged(SessionSetImpl* parent);

        void onListen(void* argument) override;

        SessionSetImpl* parent;
    };

    struct ReapBody final: public Runable {
        explicit ReapBody(SessionSetImpl* parent);

        void run() override;

        SessionSetImpl* parent;
    };

    struct PtyReadBody final: public Runable {
        PtyReadBody(SessionSetImpl* parent, u64 sessionId, PtyHandle& handle, Vterm& terminal);

        void run() override;

        SessionSetImpl* parent;
        u64 sessionId;
        PtyHandle* handle;
        Vterm* terminal;
    };

    struct PtyEofReady final: public plt::TimerCallback {
        explicit PtyEofReady(SessionSetImpl* parent);

        void ready() override;

        SessionSetImpl* parent;
    };

    // The window's single input handler. A terminal never joins the
    // router's chain: membership would then be what selects the active
    // one, which is exactly the coupling this removes.
    struct SessionSetImpl final: public SessionSet, public InputHandler {
        explicit SessionSetImpl(Composer& composer);
        ~SessionSetImpl() noexcept;

        Vterm* activeTerminal() const override;
        size_t count() const override;
        size_t activeIndex() const override;
        StringView title(size_t index) const override;
        pid_t pid(size_t index) const override;

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
        void everyTerminalConfigChanged();
        void titleChanged(const VtermTitleChanged& event);
        void publishWindowTitle(StringView title);
        void newSession() override;
        void activate(size_t index) override;
        bool activateNext();
        bool activatePrevious();
        bool selectOrdinal(size_t ordinal);
        bool close(size_t index) override;
        bool closeActive();
        void publishSessionsChanged();
        void runReaper();
        void reapReady();
        bool canReap(Vterm* terminal) const;
        void ptyEof(u64 sessionId);
        void closeEndedSessions();

        void visiblePanes(Vector<SessionPane>& out) const override;
        bool splitFocused(SplitDirection direction) override;
        bool closeFocusedPane() override;
        bool focusNeighbour(PaneSide side) override;
        void focusPane(u64 pane) override;
        Vterm* terminalAt(int pixelX, int pixelY) const override;
        void visibleSeams(stl::Vector<PixelRect>& out) const override;
        size_t cellCapacityExcept(const Vterm* except) const override;

        PaneTree& activeTree() const;
        // count_ when there is no such session, tabCount_ when no such
        // tab: the length is the one index that is never a live slot.
        size_t sessionIndex(u64 pane) const;
        size_t tabOf(u64 pane) const;
        PaneTree* takeTab();
        void openSession(u64 pane, const VtGeometry& geometry);
        // F9: how many pixels of the seam actually get painted.
        //
        // The layout gap stays zero - A10's default, and the reason all
        // six layout() calls below still pass a literal 0. The air
        // between two panes already exists without one: each pane
        // carries its own border inside its own rectangle, so two
        // neighbours put 2 x border of background between their grids
        // (paneInsets(), and the seam test in session_ut.cpp). The seam
        // is painted *into* that air rather than taking new pixels from
        // the panes, which is why turning it on moves no pane by a pixel.
        //
        // Hence the clamp: a band wider than the air would be a band
        // over somebody's text. And hence the degenerate case, which the
        // option's own help text names - with border 0 there is no air,
        // so there is nothing to paint and no seam at any width.
        u16 seamWidth() const {
            if (!composer.opts->panes) {
                return 0;
            }
            const u16 air = (u16)(2 * composer.paneInsets().left);
            const u16 asked = composer.opts->paneDividerWidth;
            return asked < air ? asked : air;
        }

        void retire(u64 pane);
        void wakeReaper();
        void dropPointerGrab();
        bool closePane(u64 pane);
        void refocus();
        void resizeExtraStore();
        void applyLayout(const PaneTree& tree);
        // T10: a surface pixel in the coordinates the pane rectangles are
        // counted in. The one place the chrome reserve comes off a pointer
        // position, so no hit test can forget it or take it twice.
        void toContentBox(int pixelX, int pixelY, int& x, int& y) const;
        // The pane under a surface pixel, or zero. The rectangles tile the
        // content box, so at most one of them holds it.
        u64 paneAt(int pixelX, int pixelY) const;
        Vterm* terminalOf(u64 pane) const;
        // The seam within grabbing distance of a surface pixel, and the
        // rectangle its share is counted in. False when the pointer is
        // over a pane rather than between two.
        bool dividerAt(int pixelX, int pixelY, PaneDivider& out) const;
        // Half the width of the strip a seam can be grabbed by, in backing
        // pixels. The seam itself has no width (A10's default: the air
        // between two panes is their own borders), so the strip is the
        // whole of what a pointer has to aim at.
        int dividerGrab(SplitDirection direction) const;
        // Moves the seam under the pointer. False when there is no drag in
        // progress or the split has gone.
        bool dragDivider(int pixelX, int pixelY);
        // The pane an event goes to: whoever took the press while a button
        // is down, so a selection that began in one pane keeps its motion
        // when the pointer wanders into the next one, and otherwise the
        // pane under the pointer.
        Vterm* pointerTarget(int pixelX, int pixelY) const;
        void setDividerCursor(bool over, SplitDirection direction);
        // A8/A10: the window's content box and the geometry of one pane
        // cut out of it both live in pane_layout.h since T5.1. They used
        // to be private members here, which made SessionSet a second
        // place that knew a chrome reserve moves a pane rather than
        // shrinking it - and put the only arithmetic that turns a
        // rectangle into a pane origin somewhere no unit test could
        // reach without building a whole session set first.
        PtySize ptySize(const VtGeometry& pane) const;

        struct Session {
            Vterm* terminal = nullptr;
            PtyHandle* handle = nullptr;
            u64 id = 0;
            // Everything the terminal is - the handle, fibers, screens -
            // dies when this arena does.
            stl::ObjPool* arena = nullptr;
            // The session's last published title, arena-owned so the
            // record stays trivially copyable when slots shift.
            stl::Buffer* title = nullptr;
        };

        struct Grave {
            stl::ObjPool* arena = nullptr;
            Vterm* terminal = nullptr;
        };

        Composer& composer;
        // Storage only: Vector has no erase and is not assignable, so
        // count_ is the live length and closing shifts the tail down.
        // Every live pane of every tab is in here; which tab holds which
        // is the trees' business.
        Vector<Session> sessions;
        size_t count_ = 0;
        // A4: one pane tree per tab. The trees are pool-owned and
        // recycled rather than freed, on the same discipline as the
        // sessions above: tabCount_ is the live length, and closing a
        // tab shifts the tail down and parks the emptied tree one past
        // the end for the next tab to take. A window whose tabs come and
        // go all afternoon therefore holds as many trees as it ever had
        // tabs at once, not as many as it ever opened.
        Vector<PaneTree*> tabs;
        size_t tabCount_ = 0;
        size_t activeTab_ = 0;
        // A5: the focused pane's terminal. Held rather than looked up so
        // that the twilight frames between the last close and the exit
        // still have a terminal to present - the same reason the flat
        // storage below never shrinks.
        Vterm* focusedTerminal_ = nullptr;
        // Closed sessions whose arena is not yet safe to drop; the reaper
        // fiber drains this. Same storage discipline as sessions.
        Vector<Grave> graves;
        size_t graveCount_ = 0;
        plt::Fiber* reaper_ = nullptr;
        Vector<u64> endedSessions;
        u64 nextSessionId_ = 1;
        PtyEofReady eofReady{this};
        plt::LoopWake* eofWake_ = nullptr;
        // Replayed into a session as it becomes active, so a terminal that
        // was not there when the window gained focus still learns of it.
        // A window is born focused until its system says otherwise, which
        // is also what the first activation used to assume.
        bool focused_ = true;
        bool pointerPresent_ = false;
        // T10, the pointer's half of the pane model. The pane that took
        // the press is held until the last button comes up: a drag that
        // began as a selection in one pane must keep reaching that pane
        // even once the pointer is over its neighbour, which is what every
        // other terminal does and what the pane it started in is still
        // drawing.
        u64 pressedPane_ = 0;
        // R2-1: why pressedPane_ is zero. It is zero for two unrelated
        // reasons - the press landed on no pane at all, or the pane it
        // landed in has since gone - and the release owes those two
        // different answers, so the value alone cannot carry the question.
        // Set when the press falls back to the active terminal; dropped by
        // dropPointerGrab() along with everything else the gesture holds,
        // which is what makes a dead pane and a mid-gesture chord answer
        // "nobody" rather than "whoever is active now".
        bool pressedFellBackToActive_ = false;
        unsigned pressedButtons_ = 0;
        // The split being dragged, and whether the pointer last stood over
        // a seam. The latter is remembered so the cursor is asked to change
        // on the crossing and not on every motion event.
        u32 draggedSplit_ = PaneTree::noNode;
        bool overDivider_ = false;
        CallSessionAction copyAction{this, InputActions::Copy};
        CallSessionAction pasteAction{this, InputActions::Paste};
        CallSessionAction pastePrimaryAction{this, InputActions::PastePrimary};
        CallSessionAction pageUpAction{this, InputActions::PageUp};
        CallSessionAction pageDownAction{this, InputActions::PageDown};
        CallSessionAction clearAction{this, InputActions::Clear};
        CallSessionsResize resizeAction{this};
        CallSessionsFontChanged fontChangedAction{this};
        CallSessionsConfigChanged configChangedAction{this};
        CallTitleChanged titleChangedAction{this};
        CallSessionAction newTabAction{this, InputActions::NewTab};
        CallSessionAction closeTabAction{this, InputActions::CloseTab};
        CallSessionAction splitVerticalAction{this, InputActions::SplitVertical};
        CallSessionAction splitHorizontalAction{this, InputActions::SplitHorizontal};
        CallSessionAction prevTabAction{this, InputActions::PrevTab};
        CallSessionAction nextTabAction{this, InputActions::NextTab};
        CallSessionAction selectTabActions[9]{
            {this, InputActions::SelectTab1},
            {this, InputActions::SelectTab2},
            {this, InputActions::SelectTab3},
            {this, InputActions::SelectTab4},
            {this, InputActions::SelectTab5},
            {this, InputActions::SelectTab6},
            {this, InputActions::SelectTab7},
            {this, InputActions::SelectTab8},
            {this, InputActions::SelectTab9},
        };
        CallSessionAction wordLeftAction{this, InputActions::WordLeft};
        CallSessionAction wordRightAction{this, InputActions::WordRight};
        CallSessionAction lineStartAction{this, InputActions::LineStart};
        CallSessionAction lineEndAction{this, InputActions::LineEnd};
        CallSessionAction killLineAction{this, InputActions::KillLine};
        CallSessionAction eraseWordAction{this, InputActions::EraseWord};
        ReapBody reapBody{this};
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

CallSessionsConfigChanged::CallSessionsConfigChanged(SessionSetImpl* parent_)
    : parent(parent_)
{
}

void CallSessionsConfigChanged::onListen(void*) {
    parent->everyTerminalConfigChanged();
}

CallTitleChanged::CallTitleChanged(SessionSetImpl* parent_)
    : parent(parent_)
{
}

void CallTitleChanged::onListen(void* argument) {
    parent->titleChanged(*static_cast<VtermTitleChanged*>(argument));
}

ReapBody::ReapBody(SessionSetImpl* parent_)
    : parent(parent_)
{
}

PtyReadBody::PtyReadBody(SessionSetImpl* parent_, u64 sessionId_, PtyHandle& handle_, Vterm& terminal_)
    : parent(parent_)
    , sessionId(sessionId_)
    , handle(&handle_)
    , terminal(&terminal_)
{
}

void PtyReadBody::run() {
    // A batch of drain blocks lands in the parser under one bookkeeping
    // wrap; the yield keeps frames and input interleaved with a flooding
    // child while the drain thread keeps the kernel busy.
    constexpr size_t batchLimit = 32;
    constexpr size_t sliceSize = 256 * 1024;
    StringView slices[batchLimit];
    size_t inSlice = 0;
    for (;;) {
        PtyHandle::Chunk* const chunks = handle->acquire();
        if (chunks == nullptr) {
            parent->ptyEof(sessionId);
            return;
        }
        PtyHandle::Chunk* chunk = chunks;
        while (chunk != nullptr) {
            size_t count = 0;
            while (chunk != nullptr && count < batchLimit) {
                slices[count] = chunk->chunk();
                inSlice += slices[count].length();
                ++count;
                chunk = chunk->next();
            }
            terminal->feedPty(slices, count);
            if (inSlice >= sliceSize) {
                inSlice = 0;
                parent->composer.platform->scheduler()->yield();
            }
        }
        handle->release(chunks);
    }
}

PtyEofReady::PtyEofReady(SessionSetImpl* parent_)
    : parent(parent_)
{
}

void PtyEofReady::ready() {
    parent->closeEndedSessions();
}

void ReapBody::run() {
    parent->runReaper();
}

SessionSetImpl::SessionSetImpl(Composer& composer_)
    : composer(composer_)
{
}

SessionSetImpl::~SessionSetImpl() noexcept {
    for (size_t at = 0; at < count_; ++at) {
        delete sessions[at].arena;
    }
    for (size_t at = 0; at < graveCount_; ++at) {
        delete graves[at].arena;
    }
    SessionSet::liveSessions = 0;
}

void SessionSetImpl::openSession(u64 pane, const VtGeometry& geometry) {
    ObjPool* const arena = ObjPool::fromMemoryRaw();
    PtyHandle* handle;
    Vterm* terminal;
    try {
        // The size goes in at spawn, not after it: a child which reads
        // TIOCGWINSZ as its first operation would race a resize() here.
        handle = composer.pty->spawn(*arena, *composer.launch, ptySize(geometry));
        // A8: the pane's grid is what the terminal is born with, which is
        // why the caller has to have placed the pane in a tree before it
        // gets here - the rectangle cannot exist before the pane does.
        terminal = Vterm::create(*arena, composer.geometry, composer.vtConfig, composer.extras, *composer.smallObjects, *composer.scheduler, *composer.host, geometry, *handle, composer.vtermTraceFactory);
    } catch (...) {
        delete arena;
        throw;
    }
    const Session session{terminal, handle, pane, arena, arena->make<Buffer>()};
    if (count_ < sessions.length()) {
        sessions.mut(count_) = session;
    } else {
        sessions.pushBack(session);
    }
    ++count_;
    SessionSet::liveSessions = (sig_atomic_t)(count_);
    handle->engage();
    PtyReadBody* const reader = arena->make<PtyReadBody>(this, pane, *handle, *terminal);
    // The parser is deep enough that this client fiber needs more than the
    // light leaf-fiber stack.
    composer.platform->scheduler()->create(*arena, *reader, 256 * 1024);
}

void SessionSetImpl::newSession() {
    PaneTree* const tree = takeTab();
    const u64 pane = nextSessionId_++;
    tree->plant(pane);
    try {
        openSession(pane, paneGeometry(composer, contentBox(composer)));
    } catch (...) {
        tree->close(pane);
        throw;
    }
    const size_t index = tabCount_++;
    activate(index);
    if (composer.window != nullptr) {
        composer.window->requestFrame();
    }
    if (composer.vtConfig.config->verbose) {
        fprintf(stderr, "%s: session: opened, %zu tabs\n", composer.brand->identifierCString(), tabCount_);
    }
}

PaneTree* SessionSetImpl::takeTab() {
    if (tabCount_ < tabs.length()) {
        return tabs[tabCount_];
    }
    PaneTree* const tree = composer.pool->make<PaneTree>();
    tabs.pushBack(tree);
    return tree;
}

void SessionSetImpl::retire(u64 pane) {
    const size_t index = sessionIndex(pane);
    if (index == count_) {
        return;
    }
    // The terminal leaves the screen before its slot is reused, or a
    // presentation keeps pointing at a record that has moved.
    sessions[index].terminal->hide();
    const Grave grave{sessions[index].arena, sessions[index].terminal};
    for (size_t at = index; at + 1 < count_; ++at) {
        sessions.mut(at) = sessions[at + 1];
    }
    --count_;
    SessionSet::liveSessions = (sig_atomic_t)(count_);
    if (graveCount_ < graves.length()) {
        graves.mut(graveCount_) = grave;
    } else {
        graves.pushBack(grave);
    }
    ++graveCount_;
    resizeExtraStore();
    // S1: the reaper is deliberately *not* rung from here. plt::Fiber::wake()
    // on a parked fiber is a switch, not a queueing (fiber.cpp:226 -> 274):
    // the reaper would run on this stack, reach canReap() and free this very
    // arena before retire() had returned - while focusedTerminal_ still named
    // the terminal inside it. Ringing belongs to whoever finishes the
    // mutation; see wakeReaper() and its two call sites.
}

void SessionSetImpl::wakeReaper() {
    // Called once the tab model is consistent again - every pane shifted,
    // every focus moved. The reaper still runs synchronously on this stack,
    // which is what the arena-drops-with-the-tab tests pin, but it now sees a
    // finished model rather than a half-mutated one.
    if (reaper_ != nullptr) {
        reaper_->wake();
    }
}

void SessionSetImpl::resizeExtraStore() {
    // A11: a store sized by the sum over the live panes has to be
    // re-sized when a pane goes, not only when one arrives. Every other
    // door into this number is a terminal announcing its own geometry,
    // and a pane that has just died announces nothing - so a budget that
    // only ever grew would keep a closed pane's share for the rest of
    // the window's life, which is the last-writer defect wearing the
    // other sign.
    composer.extras.store->setCellCount(cellCapacityExcept(nullptr));
}

bool SessionSetImpl::close(size_t index) {
    if (index >= tabCount_) {
        return tabCount_ != 0;
    }
    PaneTree* const tree = tabs[index];
    Vector<u64> panes;
    tree->panes(panes);
    for (u64 pane : panes) {
        retire(pane);
        tree->close(pane);
    }
    // The emptied tree goes back one past the live length, where
    // takeTab() will find it.
    for (size_t at = index; at + 1 < tabCount_; ++at) {
        tabs.mut(at) = tabs[at + 1];
    }
    tabs.mut(tabCount_ - 1) = tree;
    --tabCount_;
    if (tabCount_ == 0) {
        // The window is closing with this last tab; the renderer keeps
        // presenting the dead terminal until then, so its arena must not
        // drop. SessionSet retains it until its own teardown, and
        // focusedTerminal_ still names it.
        publishSessionsChanged();
        wakeReaper();
        return false;
    }
    if (index == activeTab_) {
        // The neighbour that shifted into this slot, or the new last one
        // if the tail went.
        activate(index < tabCount_ ? index : tabCount_ - 1);
    } else {
        // A background tab went; whatever the user watches stays put,
        // only its index may have shifted.
        if (index < activeTab_) {
            --activeTab_;
        }
        publishSessionsChanged();
    }
    wakeReaper();
    return true;
}

bool SessionSetImpl::closePane(u64 pane) {
    const size_t tab = tabOf(pane);
    if (tab == tabCount_) {
        return tabCount_ != 0;
    }
    PaneTree& tree = *tabs[tab];
    if (tree.count() == 1) {
        // A tab with no panes left is not a tab.
        return close(tab);
    }
    if (tab == activeTab_) {
        // S2 again: the tree the gesture is indexing into is about to lose
        // a node. A background tab losing a pane leaves the front tab's
        // drag alone.
        dropPointerGrab();
    }
    retire(pane);
    tree.close(pane);
    // The pane that took over the room hears about it; the rest of the
    // window is untouched, so no other tab is laid out again.
    applyLayout(tree);
    if (tab == activeTab_) {
        refocus();
    }
    publishSessionsChanged();
    wakeReaper();
    return true;
}

bool SessionSetImpl::closeFocusedPane() {
    if (tabCount_ == 0) {
        return false;
    }
    const u64 pane = activeTree().focused();
    return pane == 0 ? true : closePane(pane);
}

void SessionSetImpl::activate(size_t index) {
    if (index >= tabCount_) {
        return;
    }
    // S2: a gesture that began in the tab going away cannot end in the one
    // coming forward. This covers the chord taken mid-drag, which needs no
    // lost event to reach: InputBindings sits in front of this handler
    // (create(), below) and does not stand down while a button is held.
    dropPointerGrab();
    // A5: every pane that is not in the tab coming forward leaves the
    // screen, and hiding drops its input focus with it. All of them and
    // not just the outgoing tab's, because Vterm::create shows a terminal
    // as it is built: before the first activation more than one is up.
    for (size_t at = 0; at < count_; ++at) {
        if (!tabs[index]->holds(sessions[at].id)) {
            sessions[at].terminal->hide();
        }
    }
    activeTab_ = index;
    // The window's chrome follows the focused pane's title, and show()
    // republishes a terminal's title as it comes up. So which pane of
    // this tab is the focused one has to be settled before any of them
    // shows, or the window would be titled by whichever pane came up
    // last. Delivering the focus is still refocus()'s, below.
    const size_t focusedAt = sessionIndex(tabs[index]->focused());
    if (focusedAt != count_) {
        focusedTerminal_ = sessions[focusedAt].terminal;
    }
    // A5: every pane of this tab is visible. Which one takes the input is
    // a separate question, and refocus() is the only place that answers
    // it - so a neighbour pane renders without believing it is focused,
    // and a background tab believes neither.
    Vector<u64> panes;
    tabs[index]->panes(panes);
    for (u64 pane : panes) {
        const size_t at = sessionIndex(pane);
        if (at != count_) {
            sessions[at].terminal->show();
        }
    }
    refocus();
    if (composer.vtConfig.config->verbose) {
        fprintf(stderr, "%s: session: activated %zu of %zu\n", composer.brand->identifierCString(), index + 1, tabCount_);
    }
    // Every mutation of the tab model funnels through here (opening and
    // closing both end in an activation), so this is the one commit
    // point the chrome listens to.
    publishSessionsChanged();
}

void SessionSetImpl::refocus() {
    if (tabCount_ == 0) {
        return;
    }
    const size_t at = sessionIndex(activeTree().focused());
    if (at == count_) {
        return;
    }
    Vterm* const terminal = sessions[at].terminal;
    // A5: exactly one pane takes input. The pane losing it hears so even
    // though it stays on screen, which is the whole point of splitting
    // visibility from focus: its child gets a focus-out report while its
    // rows keep rendering.
    if (focusedTerminal_ != nullptr && focusedTerminal_ != terminal) {
        focusedTerminal_->focus(false);
    }
    focusedTerminal_ = terminal;
    // The window's state, replayed. A pane that was not there when the
    // window gained focus or the pointer arrived still has to hear it,
    // and a pane whose tab has just come back has had both dropped by
    // hide().
    focusedTerminal_->focus(focused_);
    focusedTerminal_->pointerPresence(pointerPresent_);
}

bool SessionSetImpl::splitFocused(SplitDirection direction) {
    // Off by default like every new feature here: nothing divides a tab
    // until the option says so.
    if (!composer.opts->panes || tabCount_ == 0) {
        return false;
    }
    PaneTree& tree = activeTree();
    const u64 pane = nextSessionId_++;
    if (!tree.split(direction, pane)) {
        return false;
    }
    // S2 again: draggedSplit_ is an index into this tree, and splitting
    // renumbers it. The same chord-during-a-drag reaches here.
    dropPointerGrab();
    // A8 again: the terminal is born with its pane's grid, so the pane
    // has to be in the tree - and therefore have a rectangle - before
    // there is a terminal to put in it.
    Vector<PanePlacement> placements;
    tree.layout(contentBox(composer), 0, placements);
    PixelRect area;
    for (const PanePlacement& placement : placements) {
        if (placement.pane == pane) {
            area = placement.area;
        }
    }
    try {
        openSession(pane, paneGeometry(composer, area));
    } catch (...) {
        tree.close(pane);
        refocus();
        throw;
    }
    // Every pane of the new layout hears its rectangle here, the newborn
    // one included: it was already born with that size, so the grid it
    // is handed is the grid it has, and paying for one redundant ioctl
    // is worth more than a placement rule with an exception in it.
    //
    // Not a no-op for it, though - paneResized() exposes the screen and
    // redraws whether the grid moved or not (vterm.cpp), so the newborn
    // pane leaves this call owing the frame every row, which is what a
    // reshaped frame is owed by every pane in it.
    applyLayout(tree);
    const size_t at = sessionIndex(pane);
    if (at != count_) {
        sessions[at].terminal->show();
    }
    refocus();
    publishSessionsChanged();
    if (composer.window != nullptr) {
        composer.window->requestFrame();
    }
    return true;
}

bool SessionSetImpl::focusNeighbour(PaneSide side) {
    if (tabCount_ == 0 || !activeTree().focusNeighbour(side)) {
        return false;
    }
    refocus();
    publishSessionsChanged();
    return true;
}

void SessionSetImpl::focusPane(u64 pane) {
    if (tabCount_ == 0 || !activeTree().holds(pane) || activeTree().focused() == pane) {
        return;
    }
    activeTree().focus(pane);
    refocus();
    publishSessionsChanged();
}

void SessionSetImpl::visiblePanes(Vector<SessionPane>& out) const {
    if (tabCount_ == 0) {
        return;
    }
    const u64 focusedPane = activeTree().focused();
    Vector<PanePlacement> placements;
    activeTree().layout(contentBox(composer), 0, placements);
    for (const PanePlacement& placement : placements) {
        const size_t at = sessionIndex(placement.pane);
        if (at == count_) {
            continue;
        }
        out.pushBack({sessions[at].terminal, placement.area, placement.pane, placement.pane == focusedPane});
    }
}

size_t SessionSetImpl::cellCapacityExcept(const Vterm* except) const {
    // A11: every live pane, background tabs included - a tab nobody is
    // looking at still parses its child's output into the shared store.
    size_t total = 0;
    for (size_t at = 0; at < count_; ++at) {
        if (sessions[at].terminal == except) {
            continue;
        }
        total += sessions[at].terminal->cellCapacity();
    }
    return total;
}

PaneTree& SessionSetImpl::activeTree() const {
    return *tabs[activeTab_];
}

size_t SessionSetImpl::sessionIndex(u64 pane) const {
    for (size_t at = 0; at < count_; ++at) {
        if (sessions[at].id == pane) {
            return at;
        }
    }
    return count_;
}

size_t SessionSetImpl::tabOf(u64 pane) const {
    for (size_t at = 0; at < tabCount_; ++at) {
        if (tabs[at]->holds(pane)) {
            return at;
        }
    }
    return tabCount_;
}

bool SessionSetImpl::closeActive() {
    return close(activeTab_);
}

void SessionSetImpl::everyTerminalResized() {
    // Background tabs track the window too: a terminal that resized only
    // on activation would replay its scrollback into wrong geometry.
    for (size_t at = 0; at < tabCount_; ++at) {
        applyLayout(*tabs[at]);
    }
}

void SessionSetImpl::applyLayout(const PaneTree& tree) {
    Vector<PanePlacement> placements;
    // The divider is zero: drawing one and dragging it are T10's, and
    // until then the panes tile the content box exactly.
    tree.layout(contentBox(composer), 0, placements);
    for (const PanePlacement& placement : placements) {
        const size_t at = sessionIndex(placement.pane);
        if (at == count_) {
            continue;
        }
        const VtGeometry geometry = paneGeometry(composer, placement.area);
        // A5-5: one geometry, two consumers. The pty size is derived from
        // the very structure the terminal was handed rather than counted
        // a second time off the window - two independent computations of
        // one quantity agree only by agreement, and the day a pane stopped
        // being the window is the day that agreement would have lapsed.
        sessions[at].terminal->paneResized(geometry);
        sessions[at].handle->resize(ptySize(geometry));
    }
}

void SessionSetImpl::everyTerminalFontChanged() {
    for (size_t at = 0; at < count_; ++at) {
        sessions[at].terminal->presentationInvalidated();
    }
}

void SessionSetImpl::everyTerminalConfigChanged() {
    for (size_t at = 0; at < count_; ++at) {
        try {
            sessions[at].terminal->configChanged();
        } catch (...) {
            // configChanged() allocates before it touches terminal state,
            // so a failure keeps this terminal on its previous
            // materialization without robbing the other sessions of the
            // reload.
        }
    }
}

void SessionSetImpl::titleChanged(const VtermTitleChanged& event) {
    // Every session's label follows its own terminal, for whatever chrome
    // projects the tab model.
    for (size_t at = 0; at < count_; ++at) {
        if (sessions[at].terminal != event.source || sessions[at].title == nullptr) {
            continue;
        }
        sessions[at].title->reset();
        sessions[at].title->append(event.title.data(), event.title.length());
        publishSessionsChanged();
        break;
    }
    // SessionSet owns visibility: a title from a pane that is not the
    // focused one is retained by its terminal but cannot replace the
    // window's chrome.
    if (focusedTerminal_ == nullptr || event.source != focusedTerminal_) {
        return;
    }
    publishWindowTitle(event.title);
}

void SessionSetImpl::publishWindowTitle(StringView title) {
    if (composer.window == nullptr) {
        return;
    }
    if (tabCount_ < 2) {
        composer.window->requestTitle(title);
        return;
    }
    Buffer decorated;
    char prefix[32];
    const int length = snprintf(prefix, sizeof(prefix), "[%zu/%zu] ", activeTab_ + 1, tabCount_);
    if (length > 0) {
        decorated.append(prefix, (size_t)(length));
    }
    decorated.append(title.data(), title.length());
    composer.window->requestTitle(StringView(decorated));
}

void SessionSetImpl::runReaper() {
    plt::Fiber* const self = composer.platform->scheduler()->current();
    for (;;) {
        if (graveCount_ == 0 || count_ == 0) {
            // With no successor there can be no exposing frame that makes
            // the last terminal safe to reap. SessionSet teardown owns all
            // remaining graves.
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
        if (canReap(grave.terminal)) {
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

bool SessionSetImpl::canReap(Vterm* terminal) const {
    // S1: a terminal the window still names as the focused one is never
    // freed, whoever asks and whenever. This is the invariant the whole
    // close path leans on rather than an ordering coincidence: it makes
    // focusedTerminal_ safe to dereference - here on the line below, and in
    // refocus() - by construction. The last tab of a window exits through
    // exactly this door: close() leaves focusedTerminal_ naming the dead
    // terminal on purpose, because the renderer keeps presenting it until
    // the window is gone, and the destructor owns that arena.
    if (terminal != nullptr && terminal == focusedTerminal_) {
        return false;
    }
    // The renderer sheds the dead terminal's retained cells only when it
    // consumes the successor's full expose; until that frame lands, its
    // cell pointers still reach into the arena. Reached from wakeReaper(),
    // focusedTerminal_ is the successor - which is the question this asks.
    if (composer.renderer != nullptr && focusedTerminal_ != nullptr && focusedTerminal_->presentationChanged()) {
        return false;
    }
    return true;
}

PtySize SessionSetImpl::ptySize(const VtGeometry& pane) const {
    return {
        .columns = pane.columns,
        .rows = pane.rows,
        .pixelWidth = (u32)(pane.columns) * composer.geometry.cellPixelWidth,
        .pixelHeight = (u32)(pane.rows) * composer.geometry.cellPixelHeight,
    };
}

void SessionSetImpl::ptyEof(u64 sessionId) {
    endedSessions.pushBack(sessionId);
    eofWake_->signal();
}

void SessionSetImpl::closeEndedSessions() {
    for (size_t ended = 0; ended < endedSessions.length(); ++ended) {
        const u64 pane = endedSessions[ended];
        if (sessionIndex(pane) == count_) {
            continue;
        }
        // A shell that exited takes its pane, not its tab: the tab only
        // goes when that was the last pane in it.
        const bool remains = closePane(pane);
        if (composer.window != nullptr) {
            if (remains) {
                composer.window->requestFrame();
            } else {
                composer.window->requestClose();
            }
        }
    }
    endedSessions.clear();
}

volatile sig_atomic_t SessionSet::liveSessions = 0;

SessionSet* SessionSet::create(Composer& composer) {
    SessionSetImpl* const sessions = composer.pool->make<SessionSetImpl>(composer);
    composer.sessions = sessions;
    // Behind InputBindings, which must keep first refusal on the chords.
    composer.inputHandlers.pushBack(sessions);
    composer.copyListeners.pushBack(&sessions->copyAction);
    composer.pasteListeners.pushBack(&sessions->pasteAction);
    composer.pastePrimaryListeners.pushBack(&sessions->pastePrimaryAction);
    composer.pageUpListeners.pushBack(&sessions->pageUpAction);
    composer.pageDownListeners.pushBack(&sessions->pageDownAction);
    composer.clearListeners.pushBack(&sessions->clearAction);
    composer.wordLeftListeners.pushBack(&sessions->wordLeftAction);
    composer.wordRightListeners.pushBack(&sessions->wordRightAction);
    composer.lineStartListeners.pushBack(&sessions->lineStartAction);
    composer.lineEndListeners.pushBack(&sessions->lineEndAction);
    composer.killLineListeners.pushBack(&sessions->killLineAction);
    composer.eraseWordListeners.pushBack(&sessions->eraseWordAction);
    composer.newTabListeners.pushBack(&sessions->newTabAction);
    composer.closeTabListeners.pushBack(&sessions->closeTabAction);
    composer.splitVerticalListeners.pushBack(&sessions->splitVerticalAction);
    composer.splitHorizontalListeners.pushBack(&sessions->splitHorizontalAction);
    composer.prevTabListeners.pushBack(&sessions->prevTabAction);
    composer.nextTabListeners.pushBack(&sessions->nextTabAction);
    for (unsigned at = 0; at < 9; ++at) {
        composer.selectTabListeners[at].pushBack(&sessions->selectTabActions[at]);
    }
    composer.resizedListeners.pushBack(&sessions->resizeAction);
    composer.fontChangedListeners.pushBack(&sessions->fontChangedAction);
    composer.configChangedListeners.pushBack(&sessions->configChangedAction);
    composer.titleChangedListeners.pushBack(&sessions->titleChangedAction);
    sessions->reaper_ = composer.platform->scheduler()->create(*composer.pool, sessions->reapBody);
    sessions->eofWake_ = composer.platform->createLoopWake(*composer.pool, sessions->eofReady);
    sessions->newSession();
    return sessions;
}

bool SessionSetImpl::activateNext() {
    if (tabCount_ < 2) {
        return false;
    }
    activate((activeTab_ + 1) % tabCount_);
    return true;
}

bool SessionSetImpl::activatePrevious() {
    if (tabCount_ < 2) {
        return false;
    }
    activate(activeTab_ == 0 ? tabCount_ - 1 : activeTab_ - 1);
    return true;
}

bool SessionSetImpl::selectOrdinal(size_t ordinal) {
    if (tabCount_ == 0) {
        return false;
    }
    // The ninth chord means "the last tab", iTerm style.
    const size_t index = ordinal == 8 ? tabCount_ - 1 : ordinal;
    if (index >= tabCount_ || index == activeTab_) {
        return false;
    }
    activate(index);
    return true;
}

size_t SessionSetImpl::count() const {
    return tabCount_;
}

size_t SessionSetImpl::activeIndex() const {
    return activeTab_;
}

StringView SessionSetImpl::title(size_t index) const {
    if (index >= tabCount_) {
        return {};
    }
    // A tab is labelled by the pane the user is typing into.
    const size_t at = sessionIndex(tabs[index]->focused());
    if (at == count_ || sessions[at].title == nullptr) {
        return {};
    }
    return StringView(*sessions[at].title);
}

pid_t SessionSetImpl::pid(size_t index) const {
    if (index >= tabCount_) {
        return -1;
    }
    // The same pane title() labels the tab by, and for the same reason:
    // a split tab describes what the user is typing into, not whichever
    // pane happens to come first in the tree. -1 rather than 0 is what
    // PtyHandle answers with when there is no child of its own, so a
    // caller has one value to test and not two.
    const size_t at = sessionIndex(tabs[index]->focused());
    if (at == count_ || sessions[at].handle == nullptr) {
        return -1;
    }
    return sessions[at].handle->childPid();
}

void SessionSetImpl::publishSessionsChanged() {
    for (IntrusiveNode* node = composer.sessionsChangedListeners.mutFront(); node != composer.sessionsChangedListeners.mutEnd();) {
        Listener* const listener = static_cast<Listener*>(node);
        node = node->next;
        listener->onListen();
    }
}

Vterm* SessionSetImpl::activeTerminal() const {
    // There is no "no sessions" state to represent: the window opens its
    // first session before its loop starts and dies the moment the last
    // one closes. This pointer is never cleared on a close, so even the
    // twilight frames between that close and the exit still have their
    // terminal.
    return focusedTerminal_;
}

void CallSessionAction::onListen(void*) {
    // R8-sec 2.1: the terminal is looked up inside each branch that uses
    // it, and deliberately not once above the switch. CloseTab's branch
    // frees a terminal on this very stack - closeFocusedPane() ends in
    // wakeReaper(), which runs the reaper synchronously - so a pointer
    // taken before the switch would outlive what it names on that path.
    // Nothing touches it there today, but that is this branch's
    // implementation being lucky rather than the shape being safe: one
    // added line in CloseTab would close the trap.
    switch (action) {
        case InputActions::Copy:
            parent->activeTerminal()->copy();
            break;
        case InputActions::Paste:
            parent->activeTerminal()->paste(false);
            break;
        case InputActions::PastePrimary:
            parent->activeTerminal()->paste(true);
            break;
        case InputActions::PageUp:
            parent->activeTerminal()->pageUp();
            break;
        case InputActions::PageDown:
            parent->activeTerminal()->pageDown();
            break;
        case InputActions::Clear:
            parent->activeTerminal()->clear();
            break;
        case InputActions::WordLeft:
            parent->activeTerminal()->sendBytes(StringView(u8"\033b"), true);
            break;
        case InputActions::WordRight:
            parent->activeTerminal()->sendBytes(StringView(u8"\033f"), true);
            break;
        case InputActions::LineStart:
            parent->activeTerminal()->sendBytes(StringView(u8"\x01"), true);
            break;
        case InputActions::LineEnd:
            parent->activeTerminal()->sendBytes(StringView(u8"\x05"), true);
            break;
        case InputActions::KillLine:
            parent->activeTerminal()->sendBytes(StringView(u8"\x15"), true);
            break;
        case InputActions::EraseWord:
            parent->activeTerminal()->sendBytes(StringView(u8"\x1b\x7f"), true);
            break;
        case InputActions::NewTab:
            parent->newSession();
            break;
        case InputActions::CloseTab:
            // With panes on, cmd+w closes the pane the user is looking
            // at and takes the tab with it only when that pane was the
            // tab's last one. With the option off every tab holds
            // exactly one pane, so the two answers coincide - the branch
            // is here because the option, not the arithmetic, is what
            // says which question was asked.
            if (parent->composer.opts->panes ? parent->closeFocusedPane() : parent->closeActive()) {
                parent->composer.window->requestFrame();
            } else {
                parent->composer.window->requestClose();
            }
            break;
        case InputActions::SplitVertical:
            // splitFocused() refuses on the option too, and requests the
            // frame itself when it does divide.
            parent->splitFocused(SplitDirection::Vertical);
            break;
        case InputActions::SplitHorizontal:
            parent->splitFocused(SplitDirection::Horizontal);
            break;
        case InputActions::PrevTab:
            if (parent->activatePrevious()) {
                parent->composer.window->requestFrame();
            }
            break;
        case InputActions::NextTab:
            if (parent->activateNext()) {
                parent->composer.window->requestFrame();
            }
            break;
        case InputActions::SelectTab1:
        case InputActions::SelectTab2:
        case InputActions::SelectTab3:
        case InputActions::SelectTab4:
        case InputActions::SelectTab5:
        case InputActions::SelectTab6:
        case InputActions::SelectTab7:
        case InputActions::SelectTab8:
        case InputActions::SelectTab9:
            if (parent->selectOrdinal((size_t)(action) - (size_t)(InputActions::SelectTab1))) {
                parent->composer.window->requestFrame();
            }
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

void SessionSetImpl::toContentBox(int pixelX, int pixelY, int& x, int& y) const {
    const Insets chrome = composer.chromeInsets();
    x = pixelX - chrome.left;
    y = pixelY - chrome.top;
}

u64 SessionSetImpl::paneAt(int pixelX, int pixelY) const {
    if (tabCount_ == 0) {
        return 0;
    }
    int x = 0;
    int y = 0;
    toContentBox(pixelX, pixelY, x, y);
    Vector<PanePlacement> placements;
    activeTree().layout(contentBox(composer), 0, placements);
    for (const PanePlacement& placement : placements) {
        const PixelRect& area = placement.area;
        if (x >= area.x && y >= area.y && x < area.x + area.width && y < area.y + area.height) {
            return placement.pane;
        }
    }
    return 0;
}

Vterm* SessionSetImpl::terminalOf(u64 pane) const {
    const size_t at = sessionIndex(pane);
    return at == count_ ? nullptr : sessions[at].terminal;
}

int SessionSetImpl::dividerGrab(SplitDirection direction) const {
    // Half a cell either side of the seam, measured on the axis being
    // crossed: a whole cell to aim at, which is what the user is already
    // aiming at everywhere else in this window.
    //
    // The glyph rather than a length in points, because points are
    // Composer's to convert and this is not Composer (A1, and the guard
    // in tst/border_pixels_guard). It scales with the display for the
    // same reason the glyph does, and it gives the two axes different
    // strips - which is right, since a line of text is wider than it is
    // tall. Never below one pixel: a strip of nothing can never be
    // entered.
    return max<int>(1, (direction == SplitDirection::Vertical ? composer.geometry.cellPixelWidth : composer.geometry.cellPixelHeight) / 2);
}

bool SessionSetImpl::dividerAt(int pixelX, int pixelY, PaneDivider& out) const {
    if (tabCount_ == 0) {
        return false;
    }
    int x = 0;
    int y = 0;
    toContentBox(pixelX, pixelY, x, y);
    Vector<PanePlacement> placements;
    Vector<PaneDivider> dividers;
    activeTree().layout(contentBox(composer), 0, placements, &dividers);
    for (const PaneDivider& divider : dividers) {
        const PixelRect& bar = divider.area;
        const bool vertical = divider.direction == SplitDirection::Vertical;
        const int grab = dividerGrab(divider.direction);
        // Across the axis the seam spans its whole box, so that end is a
        // plain containment test; along it the seam is a line, widened
        // here into something a pointer can hit.
        const bool along = vertical ? x >= bar.x - grab && x < bar.x + bar.width + grab : y >= bar.y - grab && y < bar.y + bar.height + grab;
        const bool across = vertical ? y >= bar.y && y < bar.y + bar.height : x >= bar.x && x < bar.x + bar.width;
        if (along && across) {
            out = divider;
            return true;
        }
    }
    return false;
}

bool SessionSetImpl::dragDivider(int pixelX, int pixelY) {
    if (draggedSplit_ == PaneTree::noNode || tabCount_ == 0) {
        return false;
    }
    PaneTree& tree = activeTree();
    Vector<PanePlacement> placements;
    Vector<PaneDivider> dividers;
    tree.layout(contentBox(composer), 0, placements, &dividers);
    for (const PaneDivider& divider : dividers) {
        if (divider.split != draggedSplit_) {
            continue;
        }
        int x = 0;
        int y = 0;
        toContentBox(pixelX, pixelY, x, y);
        const bool vertical = divider.direction == SplitDirection::Vertical;
        const int extent = vertical ? divider.box.width : divider.box.height;
        if (extent <= 0) {
            return false;
        }
        // A whole cell plus the borders around it is the least a pane may
        // be left with. The minimum is in cells and therefore the caller's
        // to enforce - PaneTree counts in shares and does not know a glyph
        // from a pixel - and it is imposed on both sides at once, so a
        // divider dragged to either end stops rather than crushing the
        // pane it is dragged into.
        const Insets border = composer.paneInsets();
        const int floorPixels = vertical ? border.left + border.right + composer.geometry.cellPixelWidth : border.top + border.bottom + composer.geometry.cellPixelHeight;
        const int near = (vertical ? x - divider.box.x : y - divider.box.y);
        const int clamped = min(max(near, min(floorPixels, extent)), max(extent - floorPixels, 0));
        // Rounded up, because layout() rounds the share back down: the
        // share that puts the seam on the pixel under the pointer is the
        // smallest one whose floor reaches it, and taking the floor here
        // as well would leave the seam a pixel behind the pointer on
        // nearly every drag - and would let the clamp above ask for one
        // cell and get none.
        const u64 span = (u64)(extent);
        tree.setShare(draggedSplit_, (u32)(((u64)(clamped)*PaneTree::shareScale + span - 1) / span));
        // Every pane of the tab is laid out again and told its new size,
        // which is what hands both shells a fresh SIGWINCH and marks every
        // grid fully damaged for the next frame.
        applyLayout(tree);
        if (composer.window != nullptr) {
            composer.window->requestFrame();
        }
        return true;
    }
    return false;
}

void SessionSetImpl::setDividerCursor(bool over, SplitDirection direction) {
    if (over == overDivider_ || composer.window == nullptr) {
        return;
    }
    overDivider_ = over;
    // Leaving a seam puts back the terminal's own resting cursor rather
    // than the platform default: the pointer is over a grid, and that is
    // what a grid asks for (vterm.cpp, refreshHyperlink).
    composer.window->requestPointerIcon(!over ? plt::PointerIcon::Text : (direction == SplitDirection::Vertical ? plt::PointerIcon::ResizeColumn : plt::PointerIcon::ResizeRow));
}

void SessionSetImpl::dropPointerGrab() {
    // S2: the press-held pane, the held buttons and the seam being dragged
    // are one gesture, and a gesture the window can no longer see the end of
    // is over. Left standing, pressedButtons_ keeps pointerButton()'s
    // focus-moving branch shut for good and pointerTarget() keeps answering
    // the pane the press landed in, wherever the pointer goes; and
    // draggedSplit_ is a node index with no tab attached, so a tab switch
    // mid-drag would move the seam of whichever tab happened to have a node
    // by that number - and hand every shell behind it a SIGWINCH nobody
    // asked for.
    //
    // The terminals need no telling: their own held buttons and open
    // selection are dropped by VtermImpl::hide() and by the focus(false)
    // that reaches them on these same paths.
    pressedPane_ = 0;
    pressedFellBackToActive_ = false;
    pressedButtons_ = 0;
    draggedSplit_ = PaneTree::noNode;
}

void SessionSetImpl::visibleSeams(Vector<PixelRect>& out) const {
    out.clear();
    const u16 width = seamWidth();
    if (width == 0 || tabCount_ == 0) {
        return;
    }
    Vector<PanePlacement> placements;
    Vector<PaneDivider> dividers;
    activeTree().layout(contentBox(composer), 0, placements, &dividers);
    for (const PaneDivider& divider : dividers) {
        // The seam layout reports has no width of its own - the gap is
        // zero, which is A10's default - so it names a line. The band is
        // that line grown by half the width each way, which is what puts
        // it in the middle of the air rather than against one pane.
        const u16 before = (u16)(width / 2);
        PixelRect band = divider.area;
        if (divider.direction == SplitDirection::Vertical) {
            band.x = (u16)(band.x > before ? band.x - before : 0);
            band.width = width;
        } else {
            band.y = (u16)(band.y > before ? band.y - before : 0);
            band.height = width;
        }
        out.pushBack(band);
    }
}

Vterm* SessionSetImpl::terminalAt(int pixelX, int pixelY) const {
    Vterm* const under = terminalOf(paneAt(pixelX, pixelY));
    return under != nullptr ? under : activeTerminal();
}

Vterm* SessionSetImpl::pointerTarget(int pixelX, int pixelY) const {
    if (pressedPane_ != 0) {
        Vterm* const held = terminalOf(pressedPane_);
        if (held != nullptr) {
            return held;
        }
    }
    // No press outstanding, so the pixel is the whole answer.
    return terminalAt(pixelX, pixelY);
}

bool SessionSetImpl::pointerMotion(const plt::PointerMotionInput& input) {
    if (!composer.opts->panes) {
        return activeTerminal()->pointerMotion(input);
    }
    if (dragDivider(input.pixelX, input.pixelY)) {
        return true;
    }
    PaneDivider divider;
    // Only while nothing is held down: mid-selection the pointer crosses
    // seams all the time and the cursor must not flicker with it.
    const bool overSeam = pressedButtons_ == 0 && dividerAt(input.pixelX, input.pixelY, divider);
    setDividerCursor(overSeam, divider.direction);
    if (overSeam) {
        return true;
    }
    return pointerTarget(input.pixelX, input.pixelY)->pointerMotion(input);
}

bool SessionSetImpl::pointerButton(const plt::PointerButtonInput& input) {
    if (!composer.opts->panes) {
        return activeTerminal()->pointerButton(input);
    }
    const unsigned mask = 1u << (unsigned)(input.button);
    if (!input.pressed) {
        pressedButtons_ &= ~mask;
        if (pressedButtons_ == 0) {
            const bool wasDragging = draggedSplit_ != PaneTree::noNode;
            draggedSplit_ = PaneTree::noNode;
            const u64 pane = pressedPane_;
            const bool fellBackToActive = pressedFellBackToActive_;
            pressedPane_ = 0;
            pressedFellBackToActive_ = false;
            // The release belongs to whoever got the press, even if the
            // pointer has since left that pane: a selection ends where the
            // button comes up, not where the pixel is.
            //
            // R8-test: and that includes a press that landed on no pane at
            // all. The window's chrome reserve is real pixels - a sidebar
            // or a titlebar strip puts them there (ui_sidebar_tabs.mm,
            // ui_csd_tabs.mm), and contentBox() takes them out before the
            // panes divide what is left - so a click can start outside
            // every pane. pointerTarget() delivers such a press to the
            // active terminal by its own fallback; the release has to
            // follow it there, or a program that asked for button reports
            // sees the press, never the release, and holds a button down
            // for the rest of its life.
            //
            // A pane that has since died is the other way this ends up
            // with nothing to deliver to, and it is not the same case:
            // that release goes nowhere, because handing it to a surviving
            // terminal would be a release for a press that terminal never
            // saw. R2-1: the two cases are told apart by
            // pressedFellBackToActive_ and not by pressedPane_ being zero,
            // because dropPointerGrab() zeroes pressedPane_ when the pane
            // dies - so the value alone says "zero" to both questions and
            // used to answer the second one with the survivor.
            Vterm* const held = pane != 0 ? terminalOf(pane) : (fellBackToActive ? activeTerminal() : nullptr);
            if (wasDragging || held == nullptr) {
                return wasDragging;
            }
            return held->pointerButton(input);
        }
    }
    if (input.pressed && pressedButtons_ == 0) {
        PaneDivider divider;
        if (dividerAt(input.pixelX, input.pixelY, divider)) {
            pressedButtons_ |= mask;
            draggedSplit_ = divider.split;
            return true;
        }
        // A click anywhere in a pane is what moves the focus, before the
        // press reaches the terminal: the pane that gets the press is the
        // pane the following keystrokes go to.
        const u64 pane = paneAt(input.pixelX, input.pixelY);
        if (pane != 0) {
            focusPane(pane);
            pressedPane_ = pane;
        } else {
            // No pane under the press, so pointerTarget() will hand it to
            // the active terminal. Remembered as its own fact, because the
            // release has to know that this is why pressedPane_ stayed
            // zero.
            pressedFellBackToActive_ = true;
        }
    }
    if (input.pressed) {
        pressedButtons_ |= mask;
    }
    if (draggedSplit_ != PaneTree::noNode) {
        return true;
    }
    return pointerTarget(input.pixelX, input.pixelY)->pointerButton(input);
}

bool SessionSetImpl::scroll(const plt::ScrollInput& input) {
    if (!composer.opts->panes) {
        return activeTerminal()->scroll(input);
    }
    // The wheel goes where the pointer is and does not move the focus,
    // which is how every other terminal and every scrollable window
    // behaves - reading a neighbour must not take the keyboard away from
    // what is running in front of you.
    return pointerTarget(input.pixelX, input.pixelY)->scroll(input);
}

void SessionSetImpl::focus(bool focused) {
    // A5: the window's focus event reaches the focused pane and only it.
    // The neighbours stay visible and stay unfocused, which is what their
    // children are told.
    focused_ = focused;
    if (!focused) {
        // The window will not be shown the release: whatever the pointer
        // does next belongs to whoever has the focus now.
        dropPointerGrab();
    }
    activeTerminal()->focus(focused);
}

void SessionSetImpl::pointerPresence(bool present) {
    pointerPresent_ = present;
    if (!present) {
        // The pointer left the window: whatever seam it was over, it is
        // not over it now, so the next entry has to decide the cursor
        // afresh rather than inherit a crossing that never happened.
        overDivider_ = false;
        dropPointerGrab();
    }
    activeTerminal()->pointerPresence(present);
}

void SessionSetImpl::flush() {
    activeTerminal()->flush();
}
