/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

/* This file is part of Zutty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * See the file LICENSE.GPL3 for the full license.
 */

#pragma once
#include <std/sys/types.h>

#include <cstddef>
#include <cstdint>
#include <sys/types.h>
#include <unistd.h>

struct Composer;

struct Pty {
    virtual int fd() const = 0;
    virtual ssize_t read(u8* buffer, size_t size) = 0;
    virtual ssize_t write(const u8* buffer, size_t size) = 0;
    virtual void resize(u16 columns, u16 rows) = 0;

    // Takes ownership of an already-open PTY master.
    static Pty* adopt(Composer& composer, int fd);
};

pid_t pty_fork(int& o_ptyFd, int cols, int rows);

void pty_resize(int ptyFd, int cols, int rows);
