/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

/* part of this file is part of Zutty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * See the file LICENSE.GPL3 for the full license.
 */

#include "base.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <ostream>

#include <unistd.h>

namespace stl {}

using namespace stl;

namespace {
    int origFds[3] = {0, 0, 0};
    const int targetFds[3] = {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO};

    void saveFds() {
        for (int i = 0; i < 3; ++i) {
            origFds[i] = dup(targetFds[i]);
            if (origFds[i] < 0) {
                sysError("dup");
            }
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
            sysError("dup2");
        }
    }

    if (fd != targetFds[0] && fd != targetFds[1] && fd != targetFds[2]) {
        close(fd);
    }
}

void sysError(const char* message, const char* detail) {
    const int ec = errno;
    restoreFds();
    fprintf(stderr, "Error: %s%s: %s (errno=%d)\n", message, detail != nullptr ? detail : "", strerror(ec), ec);
    exit(1);
}

void sysWarn(const char* message) {
    const int ec = errno;
    fprintf(stderr, "Warning: %s: %s (errno=%d)\n", message, strerror(ec), ec);
}

std::ostream& operator<<(std::ostream& os, const Color& c) {
    os << "rgb:" << std::hex << std::setfill('0') << std::setw(2) << (int)c.red << std::setw(2) << (int)c.red << "/" << std::setw(2) << (int)c.green << std::setw(2) << (int)c.green << "/" << std::setw(2) << (int)c.blue << std::setw(2) << (int)c.blue;
    return os;
}

Point::Point(int x_, int y_)
    : x(x_)
    , y(y_)
{
}

std::ostream& operator<<(std::ostream& os, const Point& p) {
    os << "(" << p.x << "," << p.y << ")";
    return os;
}

Rect::Rect(Point tl_, Point br_)
    : tl(tl_)
    , br(br_)
{
}

Rect::Rect(int x, int y)
    : tl(x, y)
    , br(x + 1, y)
{
}

Rect::Rect(int x1, int y1, int x2, int y2)
    : tl(x1, y1)
    , br(x2, y2)
{
}

void Rect::clear() {
    tl = Point();
    br = Point();
}

std::ostream& operator<<(std::ostream& os, const Rect& r) {
    os << "Rect{tl=" << r.tl << " " << "br=" << r.br;
    if (r.rectangular) {
        os << " rectangular}";
    } else {
        os << " regular}";
    }
    return os;
}
