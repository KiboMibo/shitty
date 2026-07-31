/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

namespace stl {
    class Output;
    class ObjPool;
    class SmallObjAllocator;
}

struct Pty;

struct PtyOutputQueue {
    virtual stl::Output* append() = 0;
    // Performs at most one raw PTY write. Returns true while POLLOUT is useful.
    virtual bool flush() = 0;

    static PtyOutputQueue* create(stl::ObjPool* pool, stl::SmallObjAllocator* allocator, Pty& pty);
};
