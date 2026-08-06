/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <csignal>
#include <cstddef>

struct Composer;
struct Pty;
struct Vterm;
struct VtermTraceFactory;

// The terminals behind one window, and which of them the window shows.
//
// Composer's terminal fields keep meaning the active session's, so every
// consumer that reads composer.vterm stays correct without learning that
// sessions exist. Activating commits those fields and then relinks the
// lists whose membership - not whose contents - selects a terminal.
struct SessionSet {
    // Builds a terminal out of a fresh arena of its own, binds the pty to
    // feed it and adopts the pair. The arena is the session's unit of
    // death: after close() the reaper drops it whole - the terminal, its
    // fiber stacks, its screens - once nothing outside reaches into it.
    virtual size_t open(Pty* pty, VtermTraceFactory* traceFactory) = 0;
    // Takes an existing terminal and the pty behind it as a session,
    // appended after the last. Returns its index. An adopted terminal has
    // no arena of its own and closing it leaks it; open() is the owning
    // path.
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
