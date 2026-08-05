/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <cstddef>

struct Composer;
struct Pty;
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

    static SessionSet* create(Composer& composer);
};
