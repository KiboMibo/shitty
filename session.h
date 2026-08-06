/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/sys/types.h>

#include <csignal>
#include <cstddef>

struct Composer;
struct Pty;
struct TerminalCell;
struct Vterm;

// The terminals behind one window, and which of them the window shows.
//
// Composer's terminal fields keep meaning the active session's, so every
// consumer that reads composer.vterm stays correct without learning that
// sessions exist. Activating commits those fields and then relinks the
// lists whose membership - not whose contents - selects a terminal.
struct SessionSet {
    // Takes an existing terminal and the pty behind it as a session,
    // appended after the last. Returns its index.
    virtual size_t adopt(Vterm* terminal, Pty* pty) = 0;
    virtual size_t count() const = 0;
    virtual size_t active() const = 0;
    // Makes index the session the window shows and types into.
    virtual void activate(size_t index) = 0;
    // Steps to the next or previous session, wrapping at either end.
    // Both report whether the active session actually changed, so a lone
    // session costs nothing rather than a needless full-grid repaint.
    virtual bool activateNext() = 0;
    virtual bool activatePrevious() = 0;
    // Drops a session, activating a neighbour so the window keeps showing
    // something. Reports whether any session remains: false means the
    // last one went and the window has nothing left to show.
    virtual bool close(size_t index) = 0;
    // Closes whichever session owns this pty. The pty EOF path knows only
    // itself. Reports whether any session remains.
    virtual bool closeByPty(Pty* pty) = 0;
    // Closes the session the window is showing.
    virtual bool closeActive() = 0;
    // The number of live sessions, readable from a signal handler.
    static volatile sig_atomic_t liveSessions;

    static SessionSet* create(Composer& composer);
};

// Fills one row of cells with the tab bar: a segment per session, each
// labelled with its number, the active one inverted. Returns false when
// there is no bar - fewer than two sessions, or no room for one - and
// leaves the cells untouched.
//
// Shared so the three renderers cannot drift: each draws this row its own
// way, but none of them decides what it says.
bool buildTabBarRow(Composer& composer, TerminalCell* cells, u16 columns);
