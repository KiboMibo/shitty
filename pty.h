/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/sys/types.h>

#include <cstddef>

namespace stl {
    class Output;
}

struct Composer;
struct LaunchCommand;

// The terminal's PTY on a pair of eternal threads: a reader sleeps in
// read and hands batches to the feed fiber, a writer sleeps in write and
// drains the outgoing queue. The stream facade parks a fiber writer while
// the queue is at its bound; writers serialize through composer.ptyMutex.
struct Pty {
    virtual stl::Output* output() = 0;
    // One non-blocking attempt: accepts what the outgoing queue takes
    // right now and returns the count without ever parking the caller.
    virtual size_t tryWrite(const u8* data, size_t len) = 0;

    // Opens the PTY, starts the child, owns the master, wires resize events,
    // and starts the reader thread and the fiber that feeds the vterm.
    static Pty* create(Composer& composer, const LaunchCommand& command);
};
