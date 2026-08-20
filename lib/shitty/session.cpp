/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "session.h"

#include "brand.h"
#include "cell_extra_store.h"
#include "composer.h"
#include "input_bindings.h"
#include "input_handler.h"
#include "listener.h"
#include "options.h"
#include "pane_layout.h"
#include "pty.h"
#include "vterm.h"

#include <plt/fiber.h>
#include <plt/loop_wake.h>
#include <plt/platform.h>
#include <plt/poller.h>
#include <plt/window.h>

#include <stdio.h>

#include <std/alg/minmax.h>
#include <std/lib/buffer.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/thr/runable.h>

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
        size_t cellCapacityExcept(const Vterm* except) const override;

        PaneTree& activeTree() const;
        // count_ when there is no such session, tabCount_ when no such
        // tab: the length is the one index that is never a live slot.
        size_t sessionIndex(u64 pane) const;
        size_t tabOf(u64 pane) const;
        PaneTree* takeTab();
        void openSession(u64 pane, const PaneGeometry& geometry);
        void retire(u64 pane);
        bool closePane(u64 pane);
        void refocus();
        void resizeExtraStore();
        void applyLayout(const PaneTree& tree);
        PixelRect contentBox() const;
        PaneGeometry paneGeometry(const PixelRect& area) const;
        PtySize ptySize(const PaneGeometry& pane) const;

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
        CallSessionAction copyAction{this, InputActions::Copy};
        CallSessionAction pasteAction{this, InputActions::Paste};
        CallSessionAction pastePrimaryAction{this, InputActions::PastePrimary};
        CallSessionAction pageUpAction{this, InputActions::PageUp};
        CallSessionAction pageDownAction{this, InputActions::PageDown};
        CallSessionAction clearAction{this, InputActions::Clear};
        CallSessionsResize resizeAction{this};
        CallSessionsFontChanged fontChangedAction{this};
        CallTitleChanged titleChangedAction{this};
        CallSessionAction newTabAction{this, InputActions::NewTab};
        CallSessionAction closeTabAction{this, InputActions::CloseTab};
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

void SessionSetImpl::openSession(u64 pane, const PaneGeometry& geometry) {
    ObjPool* const arena = ObjPool::fromMemoryRaw();
    PtyHandle* handle;
    Vterm* terminal;
    try {
        handle = composer.pty->spawn(*arena, *composer.launch);
        handle->resize(ptySize(geometry));
        // A8: the pane's grid is what the terminal is born with, which is
        // why the caller has to have placed the pane in a tree before it
        // gets here - the rectangle cannot exist before the pane does.
        terminal = Vterm::create(*arena, composer, geometry, *handle, composer.vtermTraceFactory);
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
        openSession(pane, paneGeometry(contentBox()));
    } catch (...) {
        tree->close(pane);
        throw;
    }
    const size_t index = tabCount_++;
    activate(index);
    if (composer.window != nullptr) {
        composer.window->requestFrame();
    }
    if (composer.opts->verbose) {
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
    composer.cellExtras->setCellCount(cellCapacityExcept(nullptr));
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
    retire(pane);
    tree.close(pane);
    // The pane that took over the room hears about it; the rest of the
    // window is untouched, so no other tab is laid out again.
    applyLayout(tree);
    if (tab == activeTab_) {
        refocus();
    }
    publishSessionsChanged();
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
    if (composer.opts->verbose) {
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
    // A8 again: the terminal is born with its pane's grid, so the pane
    // has to be in the tree - and therefore have a rectangle - before
    // there is a terminal to put in it.
    Vector<PanePlacement> placements;
    tree.layout(contentBox(), 0, placements);
    PixelRect area;
    for (const PanePlacement& placement : placements) {
        if (placement.pane == pane) {
            area = placement.area;
        }
    }
    try {
        openSession(pane, paneGeometry(area));
    } catch (...) {
        tree.close(pane);
        refocus();
        throw;
    }
    // The panes that gave up half their room hear about it here; the new
    // one was born with its own.
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
    activeTree().layout(contentBox(), 0, placements);
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
    tree.layout(contentBox(), 0, placements);
    for (const PanePlacement& placement : placements) {
        const size_t at = sessionIndex(placement.pane);
        if (at == count_) {
            continue;
        }
        const PaneGeometry geometry = paneGeometry(placement.area);
        // A5-5: one geometry, two consumers. The pty size is derived from
        // the very structure the terminal was handed rather than counted
        // a second time off the window - two independent computations of
        // one quantity agree only by agreement, and the day a pane stopped
        // being the window is the day that agreement would have lapsed.
        sessions[at].terminal->paneResized(geometry);
        sessions[at].handle->resize(ptySize(geometry));
    }
}

PixelRect SessionSetImpl::contentBox() const {
    // A10: the window minus whatever chrome reserves, and nothing else.
    // The border is not taken out here because it is not the window's:
    // every pane carries its own inside its own rectangle, which is what
    // puts two borders' worth of gap on the seam between two panes. The
    // panes divide this box and nothing else, which is what makes a pane
    // rectangle already be in the coordinates PaneGeometry's origin is
    // counted in.
    const Insets insets = composer.chromeInsets();
    const u32 horizontal = (u32)(insets.left) + insets.right;
    const u32 vertical = (u32)(insets.top) + insets.bottom;
    return {
        .x = 0,
        .y = 0,
        .width = (u16)(composer.pixelWidth > horizontal ? composer.pixelWidth - horizontal : 0),
        .height = (u16)(composer.pixelHeight > vertical ? composer.pixelHeight - vertical : 0),
    };
}

PaneGeometry SessionSetImpl::paneGeometry(const PixelRect& area) const {
    // A10: the rectangle is the pane's outside, so the pane's own border
    // comes off here - the very paneInsets() the backend adds back when
    // it places the grid inside that rectangle (render.h, surfacePane()).
    // Deliberately not gridColumns()/gridRows(): those take the window's
    // insets out, and the chrome's share is already out of the box these
    // rectangles divide.
    //
    // Saturating for the same reason contentBox() is: a border wider than
    // the pane must leave an empty grid rather than wrap into a huge one.
    //
    // With one pane the area is the whole content box, so chrome and
    // border come off in exactly the two steps Composer::resize() does in
    // one - a window of one pane keeps the grid it always had.
    //
    // The origin stays the rectangle's and not the grid's: every consumer
    // of it (mouse_frontend's contentLeft()) counts from contentInsets(),
    // which already carries the border this just took out.
    const Insets insets = composer.paneInsets();
    const u32 horizontal = (u32)(insets.left) + insets.right;
    const u32 vertical = (u32)(insets.top) + insets.bottom;
    const u32 width = area.width > horizontal ? area.width - horizontal : 0;
    const u32 height = area.height > vertical ? area.height - vertical : 0;
    return {
        .columns = (u16)(max<u32>(1, width / max<u32>(1, composer.glyphWidth))),
        .rows = (u16)(max<u32>(1, height / max<u32>(1, composer.glyphHeight))),
        .originX = area.x,
        .originY = area.y,
    };
}

void SessionSetImpl::everyTerminalFontChanged() {
    for (size_t at = 0; at < count_; ++at) {
        sessions[at].terminal->fontChanged();
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
    (void)(terminal);
    // The renderer sheds the dead terminal's retained cells only when it
    // consumes the successor's full expose; until that frame lands, its
    // cell pointers still reach into the arena.
    if (composer.renderer != nullptr && focusedTerminal_ != nullptr && focusedTerminal_->presentationChanged()) {
        return false;
    }
    return true;
}

PtySize SessionSetImpl::ptySize(const PaneGeometry& pane) const {
    return {
        .columns = pane.columns,
        .rows = pane.rows,
        .pixelWidth = (u32)(pane.columns) * composer.glyphWidth,
        .pixelHeight = (u32)(pane.rows) * composer.glyphHeight,
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
    composer.prevTabListeners.pushBack(&sessions->prevTabAction);
    composer.nextTabListeners.pushBack(&sessions->nextTabAction);
    for (unsigned at = 0; at < 9; ++at) {
        composer.selectTabListeners[at].pushBack(&sessions->selectTabActions[at]);
    }
    composer.resizedListeners.pushBack(&sessions->resizeAction);
    composer.fontChangedListeners.pushBack(&sessions->fontChangedAction);
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
        case InputActions::Clear:
            terminal->clear();
            break;
        case InputActions::WordLeft:
            terminal->sendBytes(StringView(u8"\033b"), true);
            break;
        case InputActions::WordRight:
            terminal->sendBytes(StringView(u8"\033f"), true);
            break;
        case InputActions::LineStart:
            terminal->sendBytes(StringView(u8"\x01"), true);
            break;
        case InputActions::LineEnd:
            terminal->sendBytes(StringView(u8"\x05"), true);
            break;
        case InputActions::KillLine:
            terminal->sendBytes(StringView(u8"\x15"), true);
            break;
        case InputActions::EraseWord:
            terminal->sendBytes(StringView(u8"\x1b\x7f"), true);
            break;
        case InputActions::NewTab:
            parent->newSession();
            break;
        case InputActions::CloseTab:
            if (parent->closeActive()) {
                parent->composer.window->requestFrame();
            } else {
                parent->composer.window->requestClose();
            }
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
    // A5: the window's focus event reaches the focused pane and only it.
    // The neighbours stay visible and stay unfocused, which is what their
    // children are told.
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
