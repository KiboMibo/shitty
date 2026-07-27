/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/sys/types.h>

#include <cstddef>

struct Composer;
struct pollfd;

enum PollMode {
    PollRead = 1 << 0,
    PollWrite = 1 << 1,
    PollError = 1 << 2,
    PollHangup = 1 << 3,
};

struct FDReady {
    int fd;
    int what;
};

struct Poller {
    virtual void arm(int fd, int mode) = 0;
    virtual void disarm(int fd) = 0;
    virtual void timeout(u64 microseconds) = 0;
    virtual void deadline(u64 monotonicMicroseconds) = 0;
    virtual int poll(struct pollfd* fds, size_t count, double* timeout) = 0;

    static Poller* create(Composer& composer);
};
