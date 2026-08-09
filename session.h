/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <signal.h>
#include <stddef.h>

struct Composer;
struct Pty;
struct PtyHandle;
struct LaunchCommand;
struct Vterm;
struct VtermTraceFactory;

// The terminals behind one window, and which of them the window shows.
//
// activeTerminal() is the only way to name the window's terminal;
// composer.pty mirrors only the active session's handle for clients which
// need that endpoint. Activating commits both together.
struct SessionSet {
    // Spawns a handle and builds its terminal and I/O fiber in one fresh
    // arena. The arena is the session's unit of death: dropping it ends
    // the client fibers, hangs up the child and frees the terminal.
    virtual size_t open(Pty& pty, const LaunchCommand& command, VtermTraceFactory* traceFactory) = 0;
    // Takes an existing terminal and the handle behind it as a session,
    // appended after the last. Returns its index. An adopted terminal has
    // no arena of its own and closing it leaks it; open() is the owning
    // path.
    virtual size_t adopt(Vterm* terminal, PtyHandle* handle) = 0;
    virtual size_t count() const = 0;
    virtual size_t active() const = 0;
    // The active session's terminal - the one authoritative answer to
    // "which terminal is the window's"; there is deliberately no
    // composer-level cache of it to go stale. Never null: a window has a
    // session before its loop starts and dies with its last one, and the
    // slot outlives even that close for the twilight frames.
    virtual Vterm* activeTerminal() const = 0;
    // The PTY handle behind session index; index must be a live session.
    virtual PtyHandle* handleAt(size_t index) const = 0;
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
    // Closes the session the window is showing.
    virtual bool closeActive() = 0;
    // The number of live sessions, readable from a signal handler.
    static volatile sig_atomic_t liveSessions;

    static SessionSet* create(Composer& composer);
};
