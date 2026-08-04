/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "fd_redirect.h"

#include <std/ios/sys.h>
#include <std/str/view.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <unistd.h>

using namespace stl;

namespace {
    static int originalFds[3] = {0, 0, 0};
    static const int targetFds[3] = {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO};

    static void saveFds() {
        for (int index = 0; index < 3; ++index) {
            originalFds[index] = dup(targetFds[index]);
            if (originalFds[index] < 0) {
                sysError("dup");
            }
        }
    }
}

void restoreFds() {
    for (int index = 0; index < 3; ++index) {
        if (originalFds[index]) {
            dup2(originalFds[index], targetFds[index]);
            close(originalFds[index]);
        }
    }
}

void redirectFds(int fd) {
    saveFds();

    for (int index = 0; index < 3; ++index) {
        if (dup2(fd, targetFds[index]) != targetFds[index]) {
            sysError("dup2");
        }
    }

    if (fd != targetFds[0] && fd != targetFds[1] && fd != targetFds[2]) {
        close(fd);
    }
}

void sysError(const char* message, const char* detail) {
    const int error = errno;
    restoreFds();

    OutBuf output(stderrStream());
    output << StringView(u8"Error: ") << StringView(message);
    if (detail != nullptr) {
        output << StringView(detail);
    }
    output << StringView(u8": ") << StringView(strerror(error)) << StringView(u8" (errno=") << (i64)(error) << StringView(u8")") << endL << finI;
    exit(1);
}

void sysWarn(const char* message) {
    const int error = errno;
    sysE << StringView(u8"Warning: ") << StringView(message) << StringView(u8": ") << StringView(strerror(error)) << StringView(u8" (errno=") << (i64)(error) << StringView(u8")") << endL;
}
