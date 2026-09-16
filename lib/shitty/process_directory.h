/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/lib/buffer.h>

#include <sys/types.h>

// The working directory of a live process, asked of the kernel rather
// than of the process's cooperation. OSC 7 is the usual route and it is
// a dead end here: on macOS only /etc/zshrc_Apple_Terminal installs
// update_terminal_cwd, and it is sourced only under Apple's own
// terminal, so a window of ours never sees the escape at all. The
// process's own cwd is what iTerm2 and Ghostty read, it needs no shell
// integration, and it is exactly what `cd` moves.
//
// One implementation for both platforms - proc_pidinfo on darwin,
// /proc/<pid>/cwd on Linux - because two callers want the same answer:
// the sidebar's folder line, and a new tab, which opens where the active
// one is. False means "no directory to be had": no such process, or one
// this user may not inspect, or a directory the kernel no longer has a
// path for. out is left empty in that case.
bool processDirectory(pid_t pid, stl::Buffer& out);
