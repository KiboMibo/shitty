/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "startup.h"

#include <cstdlib>
#include <cstring>
#include <limits.h>
#include <pwd.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace {
    bool executable(const char* path) {
        struct stat info{};
        return path != nullptr && stat(path, &info) == 0 && (info.st_mode & S_IXUSR);
    }

    std::string resolveShell(std::string path) {
        char resolved[PATH_MAX];
        if (!path.empty() && path[0] == '/') {
            return path;
        }
        if (!path.empty() && path[0] == '.' && realpath(path.c_str(), resolved) != nullptr) {
            return resolved;
        }

        const char* pathValue = getenv("PATH");
        char* search = pathValue != nullptr ? strdup(pathValue) : nullptr;
        if (search != nullptr) {
            for (char* part = std::strtok(search, ":"); part != nullptr; part = std::strtok(nullptr, ":")) {
                const std::string candidate = std::string(part) + "/" + path;
                if (realpath(candidate.c_str(), resolved) != nullptr) {
                    free(search);
                    return resolved;
                }
            }
            free(search);
        }

        const char* fallback = getenv("SHELL");
        if (executable(fallback)) {
            return fallback;
        }
        const passwd* entry = getpwuid(getuid());
        fallback = entry != nullptr ? entry->pw_shell : nullptr;
        return executable(fallback) ? fallback : "/bin/sh";
    }

    std::string validateShell(const std::string& requested) {
        const std::string path = resolveShell(requested);
        for (char* permitted = getusershell(); permitted != nullptr; permitted = getusershell()) {
            if (path == permitted) {
                endusershell();
                setenv("SHELL", path.c_str(), 1);
                return path;
            }
        }
        endusershell();
        unsetenv("SHELL");
        return path;
    }

    std::string shellArgv0(const std::string& path, bool login) {
        const size_t separator = path.find_last_of('/');
        std::string name = separator == std::string::npos ? path : path.substr(separator + 1);
        if (login) {
            name.insert(name.begin(), '-');
        }
        return name;
    }
}

LaunchCommand buildLaunchCommand(int argc, char* argv[], const char* defaultShell, bool login) {
    LaunchCommand command;
    if (argc > 2 && std::strcmp(argv[1], "-e") == 0) {
        command.executable = argv[2];
        for (int index = 2; index < argc; ++index) {
            command.arguments.emplace_back(argv[index]);
        }
        return command;
    }

    const char* selected = argc == 2 ? argv[1] : defaultShell;
    if (selected == nullptr || selected[0] == '\0') {
        throw std::runtime_error("empty shell command");
    }
    command.executable = validateShell(selected);
    command.arguments.push_back(shellArgv0(command.executable, login));
    return command;
}

void configureTerminalChildEnvironment() {
    if (setenv("TERM", "xterm-256color", 1) < 0 || setenv("SHITTY_VERSION", SHITTY_VERSION, 1) < 0) {
        throw std::runtime_error("cannot configure terminal child environment");
    }
}
