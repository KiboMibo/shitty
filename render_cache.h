/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "terminal_types.h"

#include <std/sys/types.h>

struct Composer;

struct RenderCacheCallback {
    virtual void render(const TerminalCell* input, RenderCell* output, u16 count, u8 lineAttribute) const = 0;
};

struct RenderCache {
    virtual void beginFrame(u16 columns, u64 context) = 0;
    virtual const RenderCell* render(const TerminalCell* cells, u16 count, u8 lineAttribute, RenderCell* scratch, const RenderCacheCallback& callback) = 0;

    static RenderCache* create(Composer& composer);
};
