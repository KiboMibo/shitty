/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

// Report a failed system call with strerror(errno) on the original stderr
// and terminate. Restores the standard descriptors first: in the terminal
// child they are redirected to the PTY slave by redirectFds().
[[noreturn]] void sysError(const char* message, const char* detail = nullptr);
void sysWarn(const char* message);

void redirectFds(int fd);
void restoreFds();
