/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/lib/buffer.h>
#include <std/lib/vector.h>
#include <std/sys/types.h>

// The exec image: NUL-terminated strings appended back to back in
// storage, addressed by offsets so the structure stays valid across
// moves. offsets lists argv in order; executableOffset names the path
// to exec.
struct LaunchCommand {
    stl::Buffer storage;
    stl::Vector<u32> offsets;
    u32 executableOffset = 0;

    const char* executable() const;
    const char* argument(size_t index) const;
};

LaunchCommand buildLaunchCommand(int argc, char* argv[], const char* defaultShell, bool login);

void configureTerminalChildEnvironment();
