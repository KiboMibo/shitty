/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <signal.h>

struct Composer;
struct Vterm;

// The terminals behind one window, and which of them the window shows.
struct SessionSet {
    // The active session's terminal - the one authoritative answer to
    // "which terminal is the window's". Session creation, selection and
    // death are driven by the tab actions registered at create().
    virtual Vterm* activeTerminal() const = 0;
    // The number of live sessions, readable from a signal handler.
    static volatile sig_atomic_t liveSessions;

    static SessionSet* create(Composer& composer);
};
