/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/sys/types.h>

#include <cstddef>

namespace stl {
    class Input;
    class Output;
}

struct Composer;
struct LaunchCommand;

// The terminal's PTY as a pair of blocking stream facades. Both sides run
// on fibers: a read that would block parks the calling fiber until the
// descriptor is readable, a write parks it until the kernel accepts the
// remaining bytes. Writers serialize through composer.ptyMutex.
struct Pty {
    virtual stl::Input* input() = 0;
    virtual stl::Output* output() = 0;
    // One non-blocking attempt: accepts what the kernel takes right now
    // and returns the count without ever parking the caller.
    virtual size_t tryWrite(const u8* data, size_t len) = 0;

    // Opens the PTY, starts the child, owns the master, wires resize events,
    // and spawns the fiber that feeds terminal output into the vterm.
    static Pty* create(Composer& composer, const LaunchCommand& command);
};
