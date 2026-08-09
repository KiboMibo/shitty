/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/sys/types.h>

namespace stl {
    class Input;
    class ObjPool;
    class Output;
}

namespace plt {
    struct Scheduler;
}

struct LaunchCommand;

struct PtySize {
    u32 columns = 0;
    u32 rows = 0;
    u32 pixelWidth = 0;
    u32 pixelHeight = 0;
};

// One child and its pseudoterminal. The handle is a pool-owned duplex
// resource: dropping its owner hangs up the child and closes the master.
// Reading and writing are scheduler-aware blocking stream operations; the
// client owns every coroutine which performs them.
struct PtyHandle {
    virtual stl::Input* input() = 0;
    virtual stl::Output* output() = 0;
    virtual void resize(const PtySize& size) = 0;
};

// Process-lifetime factory. It knows how to create OS pseudoterminals and
// children, but nothing about sessions, terminal parsers, windows or their
// lifetimes.
struct Pty {
    virtual PtyHandle* spawn(stl::ObjPool& owner, const LaunchCommand& command) = 0;
};

Pty* createPty(stl::ObjPool& owner, plt::Scheduler& scheduler);
