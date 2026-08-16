/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/sys/types.h>

#include <stddef.h>

namespace stl {
    class Output;
}

struct Composer;
struct Vterm;
struct VtermTraceFactory;

struct VtermHeadless {
    virtual void feed(const u8* data, size_t len) = 0;
    // The one terminal this host built and feeds; the host owns it for
    // the process lifetime, there is no session set to ask.
    virtual Vterm* terminal() = 0;

    // ptyCapture observes what the terminal writes toward its child;
    // null discards it.
    static VtermHeadless* create(Composer& composer, VtermTraceFactory* traceFactory, stl::Output* ptyCapture = nullptr);
};
