/* This file is part of Zutty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * See the file LICENSE for the full license.
 */

#include "log.h"

#include <iomanip>
#include <sstream>
#include <string.h>
#include <unistd.h>


namespace stl {}
using namespace stl;

const char* logFileName(const char* path) {
    const char* name = path;
    while (*path != '\0') {
        if (*path == '/' || *path == '\\') {
            name = path + 1;
        }
        ++path;
    }
    return name;
}

std::string dumpBuffer(const unsigned char* start, const unsigned char* end) {
    if (opts.quiet) {
        return "";
    }

    std::ostringstream os;
    int count = 0;
    os << "'";
    for (auto it = start; it != end; ++it) {
        switch (*it) {
            case '\a':
                os << "\\a";
                break;
            case '\b':
                os << "\\b";
                break;
            case '\x1b':
                os << "\\x1b";
                break;
            case '\f':
                os << "\\f";
                break;
            case '\n':
                os << "\\n";
                break;
            case '\r':
                os << "\\r";
                break;
            case '\t':
                os << "\\t";
                break;
            case '\v':
                os << "\\v";
                break;
            case '\x7f':
                os << "\\x7f";
                break;
            default:
                if (*it < ' ' || *it >= 0x80) {
                    os << "\\x" << std::hex << std::setw(2) << std::setfill('0') << (unsigned int)*it;
                } else {
                    os << *it;
                }
                break;
        }
        ++count;
    }
    if (count) {
        os << "' (" << count << " bytes)" << std::endl;
        return os.str();
    } else {
        return "";
    }
}

int origFds[3] = {0, 0, 0};
int targetFds[3] = {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO};

void saveFds() {
    for (int i = 0; i < 3; ++i) {
        origFds[i] = dup(targetFds[i]);
        if (origFds[i] < 0) {
            SYS_ERROR("dup ", i);
        }
    }
}

void restoreFds() {
    for (int i = 0; i < 3; ++i) {
        if (origFds[i]) {
            dup2(origFds[i], targetFds[i]);
            close(origFds[i]);
        }
    }
}

void redirectFds(int fd) {
    saveFds();

    for (int i = 0; i < 3; ++i) {
        if (dup2(fd, targetFds[i]) != targetFds[i]) {
            SYS_ERROR("dup2 to ", i);
        }
    }

    if (fd != targetFds[0] && fd != targetFds[1] && fd != targetFds[2]) {
        close(fd);
    }
}
