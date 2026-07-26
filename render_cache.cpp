/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "render_cache.h"

#include "composer.h"

#include <std/alg/minmax.h>
#include <std/lib/buffer.h>
#include <std/mem/obj_pool.h>
#include <std/str/hash.h>

#include <cstring>

using namespace stl;

namespace {
    struct RenderCacheEntry {
        u64 hash = 0;
        u32 count = 0;
        u32 lastUse = 0;
        u32 pinnedFrame = 0;
        bool valid = false;
    };

    struct RenderCacheImpl final: public RenderCache {
        void beginFrame(u16 columns, u64 context) override;
        const RenderCell* render(const TerminalCell* cells, u16 count, u8 lineAttribute, RenderCell* scratch, const RenderCacheCallback& callback) override;

        void invalidate() noexcept;

        Buffer storage_;
        RenderCacheEntry* entries_ = nullptr;
        RenderCell* cells_ = nullptr;
        u16 columns_ = 0;
        u32 entryCount_ = 0;
        u32 setCount_ = 0;
        u32 use_ = 0;
        u32 frame_ = 0;
        u64 context_ = 0;

        void configure(u16 columns);
        void allocate();
        u32 nextUse();
        RenderCell* find(const TerminalCell* cells, u16 count, u8 lineAttribute, bool& hit);
    };
}

void RenderCacheImpl::configure(u16 columns) {
    constexpr size_t byteBudget = 1024 * 1024;
    constexpr u32 ways = 4;
    constexpr u32 maximumEntries = 512;

    columns_ = columns;
    const size_t bytesPerEntry = sizeof(RenderCacheEntry) + (size_t)(columns) * sizeof(RenderCell);
    const size_t possibleEntries = bytesPerEntry == 0 ? 0 : byteBudget / bytesPerEntry;
    if (possibleEntries < ways) {
        entries_ = nullptr;
        cells_ = nullptr;
        entryCount_ = 0;
        setCount_ = 0;
        return;
    }

    u32 sets = 1;
    const u32 maximumSets = (u32)(min<size_t>(possibleEntries, maximumEntries)) / ways;
    while ((sets << 1) <= maximumSets) {
        sets <<= 1;
    }
    setCount_ = sets;
    entryCount_ = sets * ways;
    entries_ = nullptr;
    cells_ = nullptr;
    use_ = 0;
    frame_ = 0;
}

void RenderCacheImpl::allocate() {
    if (entryCount_ == 0 || entries_ != nullptr) {
        return;
    }
    const size_t entryBytes = (size_t)(entryCount_) * sizeof(RenderCacheEntry);
    const size_t cellBytes = (size_t)(entryCount_)*columns_ * sizeof(RenderCell);
    storage_.grow(entryBytes + cellBytes);
    entries_ = (RenderCacheEntry*)(storage_.mutData());
    cells_ = (RenderCell*)((u8*)(storage_.mutData()) + entryBytes);
    memset(entries_, 0, entryBytes);
}

void RenderCacheImpl::invalidate() noexcept {
    if (entries_ == nullptr) {
        return;
    }
    for (u32 index = 0; index < entryCount_; ++index) {
        entries_[index].valid = false;
    }
}

void RenderCacheImpl::beginFrame(u16 columns, u64 context) {
    if (columns_ != columns) {
        configure(columns);
    }
    allocate();
    if (++frame_ == 0) {
        for (u32 index = 0; index < entryCount_; ++index) {
            entries_[index].pinnedFrame = 0;
        }
        frame_ = 1;
    }
    if (context_ == context) {
        return;
    }
    invalidate();
    context_ = context;
}

u32 RenderCacheImpl::nextUse() {
    if (++use_ != 0) {
        return use_;
    }
    for (u32 index = 0; index < entryCount_; ++index) {
        entries_[index].lastUse = 0;
    }
    use_ = 1;
    return use_;
}

RenderCell* RenderCacheImpl::find(const TerminalCell* cells, u16 count, u8 lineAttribute, bool& hit) {
    constexpr u16 minimumCells = 8;
    constexpr u32 ways = 4;

    hit = false;
    if (count < minimumCells || entryCount_ == 0) {
        return nullptr;
    }

    u64 hash = shash64(cells, (size_t)(count) * sizeof(TerminalCell));
    hash ^= ((u64)(lineAttribute) + 1) * 0x9e3779b97f4a7c15ULL;
    const u32 first = ((u32)(hash) & (setCount_ - 1)) * ways;
    RenderCacheEntry* victim = nullptr;
    for (u32 way = 0; way < ways; ++way) {
        RenderCacheEntry& entry = entries_[first + way];
        if (entry.valid && entry.hash == hash && entry.count == count) {
            entry.lastUse = nextUse();
            entry.pinnedFrame = frame_;
            hit = true;
            return cells_ + (size_t)(first + way) * columns_;
        }
        if (entry.pinnedFrame == frame_) {
            continue;
        }
        if (!entry.valid || victim == nullptr || entry.lastUse < victim->lastUse) {
            victim = &entry;
        }
    }
    if (victim == nullptr) {
        return nullptr;
    }

    const u32 index = (u32)(victim - entries_);
    victim->hash = hash;
    victim->count = count;
    victim->lastUse = nextUse();
    victim->pinnedFrame = frame_;
    victim->valid = true;
    return cells_ + (size_t)(index)*columns_;
}

const RenderCell* RenderCacheImpl::render(const TerminalCell* cells, u16 count, u8 lineAttribute, RenderCell* scratch, const RenderCacheCallback& callback) {
    bool hit;
    RenderCell* output = find(cells, count, lineAttribute, hit);
    if (output == nullptr) {
        output = scratch;
        hit = false;
    }
    if (!hit) {
        callback.render(cells, output, count, lineAttribute);
    }
    return output;
}

RenderCache* RenderCache::create(Composer& composer) {
    return composer.pool->make<RenderCacheImpl>();
}
