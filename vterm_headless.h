/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/sys/types.h>

#include <cstddef>

struct Composer;

struct VtermHeadless {
    virtual void feed(const u8* data, size_t len) = 0;

    static VtermHeadless* create(Composer& composer);
};
