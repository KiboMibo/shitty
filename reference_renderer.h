/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/sys/types.h>

struct Composer;
struct TerminalUpdate;

struct ReferenceImage {
    const u8* pixels = nullptr;
    size_t length = 0;
    u16 width = 0;
    u16 height = 0;
};

struct ReferenceRenderer {
    virtual ReferenceImage render(const TerminalUpdate& update) = 0;

    static ReferenceRenderer* create(Composer& composer);
};
