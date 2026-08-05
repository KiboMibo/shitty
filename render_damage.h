/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/sys/types.h>

struct RenderDamage {
    struct Entry {
        u64 generation;
        u32 begin;
        u32 count;
    };

    void configure(Entry* storage, u32 capacity) noexcept;
    bool advance() noexcept;
    void add(u32 begin, u32 count) noexcept;
    void full() noexcept;
    void collect(u64 appliedGeneration) noexcept;
    bool requiresFull(u64 appliedGeneration, bool initialized) const noexcept;
    const Entry& entry(u32 index) const noexcept;

    Entry* storage = nullptr;
    u32 capacity = 0;
    u32 begin = 0;
    u32 count = 0;
    u64 generation = 0;
    u64 fullGeneration = 0;
};
