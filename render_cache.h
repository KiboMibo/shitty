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

struct RenderCacheResult {
    RenderCell* scratch;
    RenderCellSpan* spans;
};

struct RenderCache {
    virtual void beginFrame(u64 context) = 0;
    virtual RenderCacheResult render(const TerminalCell* cells, u16 count, u8 lineAttribute, u32 index, RenderCell* scratch, RenderCellSpan* spans, const RenderCacheCallback& callback) = 0;
    virtual size_t spanCapacity(u16 columns, u16 rows) const noexcept = 0;

    static RenderCache* create(Composer& composer);
};
