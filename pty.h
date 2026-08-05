/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/sys/types.h>

#include <cstddef>
#include <sys/types.h>

namespace stl {
    class Output;
}

namespace plt {
    struct FiberMutex;
}

struct Composer;
struct LaunchCommand;

// The terminal's PTY on a pair of eternal threads: a reader sleeps in
// read and hands batches to the feed fiber, a writer sleeps in write and
// drains the outgoing queue. The stream facade parks a fiber writer while
// the queue is at its bound; writers serialize through this pty's mutex.
struct Pty {
    virtual stl::Output* output() = 0;
    // Serializes writers of THIS pty's stream: its own staging fiber and
    // every transaction fiber take it before writing to output(). It
    // belongs to the pty rather than to the Composer so that the lock and
    // the stream it guards stay the same object once a window has more
    // than one terminal behind it.
    virtual plt::FiberMutex& mutex() = 0;
    // One non-blocking attempt: accepts what the outgoing queue takes
    // right now and returns the count without ever parking the caller.
    virtual size_t tryWrite(const u8* data, size_t len) = 0;
    // Ends the session behind this pty and releases everything it holds.
    // Safe whether or not the child is still alive: the child is hung up
    // first, which is what lets the reader out of its blocking read.
    virtual void stop() = 0;

    // Opens the PTY, starts the child, owns the master, wires resize events,
    // and starts the reader thread and the fiber that feeds the vterm.
    static Pty* create(Composer& composer, const LaunchCommand& command);
};

// The shell's pid once Pty::create forked it, -1 before; async-signal-safe
// to read, for the SIGCHLD handler that exits with the shell's status.
pid_t ptyChildPid();
