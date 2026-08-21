/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "pane_layout.h"

#include <std/lib/vector.h>
#include <std/str/view.h>

#include <signal.h>
#include <stddef.h>
#include <sys/types.h>

struct Composer;
struct Vterm;

// A4/A5: one pane of the active tab - the terminal it holds, where it
// sits in the window's content box, and whether it is the one taking
// input. Every pane handed out here is visible; exactly one of them has
// `focused` set.
struct SessionPane {
    Vterm* terminal = nullptr;
    PixelRect area;
    u64 id = 0;
    bool focused = false;
};

// The terminals behind one window, arranged as tabs of panes (A4), and
// which of them the window shows.
//
// The tab half of this interface is unchanged from when a tab was a
// single terminal: an index still names a tab, count() still counts
// tabs, and title() still answers with what the user reads on one. What
// moved underneath is the meaning of "the tab's terminal" - it is now
// the focused pane of that tab's pane tree.
struct SessionSet {
    // A5: the focused pane's terminal - the one authoritative answer to
    // "which terminal is the window's". Session creation, selection and
    // death are driven by the tab actions registered at create().
    virtual Vterm* activeTerminal() const = 0;
    // The tab model a window chrome projects: the live tabs in visual
    // order. Every model mutation and every title change commits its
    // state first and then notifies
    // composer.sessionsChangedListeners.
    virtual size_t count() const = 0;
    virtual size_t activeIndex() const = 0;
    // The tab's focused pane's last published title; empty until its
    // shell set one.
    virtual stl::StringView title(size_t index) const = 0;
    // The shell process behind the same pane title() labels the tab by,
    // or -1 when the tab names none. Chrome that wants to say what a tab
    // is working on - its directory, and the branch above it - asks the
    // process rather than the shell's cooperation: OSC 7 is only ever
    // installed by Apple's own zshrc, under Apple's own terminal, so it
    // never reaches this one.
    virtual pid_t pid(size_t index) const = 0;
    virtual void activate(size_t index) = 0;
    // Opens a tab holding one pane.
    virtual void newSession() = 0;
    // Closes a whole tab, panes and all. False when the closed tab was
    // the last one: the caller owns the decision to close the window.
    virtual bool close(size_t index) = 0;

    // A4/A5: the panes of the active tab in visual order, each with the
    // rectangle it occupies inside the window's content box. This is the
    // list of live panes - what a frame draws, what a pointer hit-tests
    // against, and what A11 sums a window-wide budget over.
    virtual void visiblePanes(stl::Vector<SessionPane>& out) const = 0;
    // Divides the focused pane, giving the new one the far half and the
    // focus. False when the `panes` option is off or there is nothing to
    // divide.
    virtual bool splitFocused(SplitDirection direction) = 0;
    // Closes the focused pane. When it was the tab's last one the tab
    // goes with it, and the answer is then close()'s: false when that
    // was the last tab.
    virtual bool closeFocusedPane() = 0;
    // Moves the focus to the neighbouring pane of the active tab. False
    // when there is none on that side.
    virtual bool focusNeighbour(PaneSide side) = 0;
    // Moves the focus to one named pane of the active tab; ignored when
    // the pane is not there, which a hit test on a pane that has just
    // died will ask for.
    virtual void focusPane(u64 pane) = 0;
    // The terminal of the pane occupying this surface pixel, in the same
    // space the pointer events use. Falls back to activeTerminal() for a
    // pixel outside every pane - the window's borders and, before any tab
    // exists, all of it. With one pane per tab this is always the active
    // terminal, which is why everything that delivers to "the window's
    // terminal" was right until panes arrived and has to ask here now.
    virtual Vterm* terminalAt(int pixelX, int pixelY) const = 0;
    // F9: the seams of the active tab, as bands of pixels to paint, in
    // the same content-box coordinates visiblePanes() answers in.
    //
    // A band and not a line: two neighbouring panes already leave air
    // between their grids - each carries its own border inside its own
    // rectangle - and the seam is painted into that air rather than
    // taking pixels from either pane. So a window with a divider is laid
    // out exactly like a window without one, and the width is clamped to
    // the air there is. Empty when the panes option is off, when the tab
    // holds one pane, or when there is no air to paint into - which is
    // what a border of zero means.
    virtual void visibleSeams(stl::Vector<PixelRect>& out) const = 0;

    // A11: the cells held by every live pane except one, which is how a
    // store shared by the whole window gets sized by the sum over its
    // panes instead of by whoever wrote to it last. The exception is the
    // caller, which adds its own count: it may not be in the set yet
    // when it asks, because a terminal sizes the store while it is still
    // being built.
    virtual size_t cellCapacityExcept(const Vterm* except) const = 0;

    // The number of live panes, readable from a signal handler.
    static volatile sig_atomic_t liveSessions;

    static SessionSet* create(Composer& composer);
};
