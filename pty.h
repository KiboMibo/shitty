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

// The terminal's PTY. The write side is a blocking stream facade on a
// fiber: a write parks the caller until the kernel accepts the remaining
// bytes, and writers serialize through composer.ptyMutex. The read side
// has no stream: an eternal reader thread drains the master into a buffer
// the feed fiber consumes.
struct Pty {
    virtual stl::Output* output() = 0;
    // One non-blocking attempt: accepts what the kernel takes right now
    // and returns the count without ever parking the caller.
    virtual size_t tryWrite(const u8* data, size_t len) = 0;

    // Opens the PTY, starts the child, owns the master, wires resize events,
    // and starts the reader thread and the fiber that feeds the vterm.
    static Pty* create(Composer& composer, const LaunchCommand& command);
};
