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
    class StringView;
}

namespace plt {
    struct Platform;
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
//
// engage() reroutes the handle through the factory's eternal drain
// thread: the kernel is drained into blocks off the parser thread, and
// acquire() hands the parser a chain of whole blocks in place of a byte
// stream - everything buffered so far, in arrival order, or null at
// EOF. The views stay valid until the caller gives the same chain back
// with release(); one chain may be out at a time. After engage() the
// stream input() must not be used; output() keeps its interface and
// queues blocks onto the same thread. Dropping an engaged handle's
// owner performs a brief blocking handshake with the drain before the
// master closes.
struct PtyHandle {
    struct Chunk {
        virtual stl::StringView chunk() = 0;
        virtual Chunk* next() = 0;
    };

    virtual stl::Input* input() = 0;
    virtual stl::Output* output() = 0;
    virtual void resize(const PtySize& size) = 0;
    virtual void engage() = 0;

    virtual Chunk* acquire() = 0;
    virtual void release(Chunk* chunks) = 0;
};

// Process-lifetime factory. It knows how to create OS pseudoterminals and
// children, but nothing about sessions, terminal parsers, windows or their
// lifetimes. The drain thread and its main-loop doorbell start on the
// first engage() and live until exit(); the platform may be null when no
// handle is ever engaged.
struct Pty {
    virtual PtyHandle* spawn(stl::ObjPool& owner, const LaunchCommand& command) = 0;
};

Pty* createPty(stl::ObjPool& owner, plt::Scheduler& scheduler, plt::Platform* platform = nullptr);
