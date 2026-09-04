/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <lib/vterm/pty.h>

namespace stl {
    class ObjPool;
}

namespace plt {
    struct Platform;
    struct Scheduler;
}

struct LaunchCommand;

// Process-lifetime factory. It knows how to create OS pseudoterminals and
// children, but nothing about sessions, terminal parsers, windows or their
// lifetimes. The drain thread and its main-loop doorbell start on the
// first engage() and live until exit(); the platform may be null when no
// handle is ever engaged.
struct Pty {
    // The size is set on the slave before the fork, not by a resize()
    // after it: a child which reads TIOCGWINSZ as its first operation
    // after exec would otherwise race that resize and see 0x0.
    virtual PtyHandle* spawn(stl::ObjPool& owner, const LaunchCommand& command, const PtySize& size) = 0;
};

Pty* createPty(stl::ObjPool& owner, plt::Scheduler& scheduler, plt::Platform* platform = nullptr);
