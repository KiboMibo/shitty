/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "startup.h"

#include "fatal.h"

#include <cstdlib>
#include <cstring>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace stl;

namespace {
    static bool executable(const char* path) {
        struct stat info{};
        return path != nullptr && stat(path, &info) == 0 && (info.st_mode & S_IXUSR);
    }

    static void resolveShell(const char* path, char out[PATH_MAX]) {
        if (path[0] == '/') {
            snprintf(out, PATH_MAX, "%s", path);
            return;
        }
        if (path[0] == '.' && realpath(path, out) != nullptr) {
            return;
        }

        const char* pathValue = getenv("PATH");
        char* search = pathValue != nullptr ? strdup(pathValue) : nullptr;
        if (search != nullptr) {
            char candidate[PATH_MAX];
            for (char* part = strtok(search, ":"); part != nullptr; part = strtok(nullptr, ":")) {
                snprintf(candidate, sizeof(candidate), "%s/%s", part, path);
                if (realpath(candidate, out) != nullptr) {
                    free(search);
                    return;
                }
            }
            free(search);
        }

        const char* fallback = getenv("SHELL");
        if (executable(fallback)) {
            snprintf(out, PATH_MAX, "%s", fallback);
            return;
        }
        const passwd* entry = getpwuid(getuid());
        fallback = entry != nullptr ? entry->pw_shell : nullptr;
        snprintf(out, PATH_MAX, "%s", executable(fallback) ? fallback : "/bin/sh");
    }

    static void validateShell(const char* requested, char out[PATH_MAX]) {
        resolveShell(requested, out);
        for (char* permitted = getusershell(); permitted != nullptr; permitted = getusershell()) {
            if (strcmp(out, permitted) == 0) {
                endusershell();
                setenv("SHELL", out, 1);
                return;
            }
        }
        endusershell();
        unsetenv("SHELL");
    }

    static u32 appendString(Buffer& storage, const char* text) {
        const u32 offset = (u32)(storage.used());
        storage.append(text, strlen(text) + 1);
        return offset;
    }
}

const char* LaunchCommand::executable() const {
    return (const char*)(storage.data()) + executableOffset;
}

const char* LaunchCommand::argument(size_t index) const {
    return (const char*)(storage.data()) + offsets[index];
}

LaunchCommand buildLaunchCommand(int argc, char* argv[], const char* defaultShell, bool login) {
    LaunchCommand command;
    if (argc > 2 && strcmp(argv[1], "-e") == 0) {
        for (int index = 2; index < argc; ++index) {
            command.offsets.pushBack(appendString(command.storage, argv[index]));
        }
        command.executableOffset = command.offsets[0];
        return command;
    }

    const char* selected = argc == 2 ? argv[1] : defaultShell;
    if (selected == nullptr || selected[0] == '\0') {
        raiseError(StringView(u8"empty shell command"));
    }
    char path[PATH_MAX];
    validateShell(selected, path);
    command.executableOffset = appendString(command.storage, path);

    // argv0 is the shell's base name, '-' prefixed for a login shell.
    const char* separator = strrchr(path, '/');
    const char* name = separator != nullptr ? separator + 1 : path;
    const u32 argv0 = (u32)(command.storage.used());
    if (login) {
        command.storage.append("-", 1);
    }
    command.storage.append(name, strlen(name) + 1);
    command.offsets.pushBack(argv0);
    return command;
}

void configureTerminalChildEnvironment() {
    if (setenv("TERM", "xterm-256color", 1) < 0 || setenv("SHITTY_VERSION", SHITTY_VERSION, 1) < 0) {
        raiseError(StringView(u8"cannot configure terminal child environment"));
    }
}
