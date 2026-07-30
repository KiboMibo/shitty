/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/sys/types.h>

struct Composer;
struct LaunchCommand;

struct Pty {
    virtual ssize_t write(const u8* buffer, size_t size) = 0;
    virtual void outputReady() = 0;

    // Opens the PTY, starts the child, owns the master, wires resize events,
    // and registers itself with the platform poller.
    static Pty* create(Composer& composer, const LaunchCommand& command);
};
