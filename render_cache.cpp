/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "render_cache.h"

#include "composer.h"

#include <std/lib/buffer.h>
#include <std/lib/list.h>
#include <std/lib/vector.h>
#include <std/mem/new.h>
#include <std/mem/obj_pool.h>
#include <std/str/hash.h>
#include <std/typ/intrin.h>

using namespace stl;

namespace {
    constexpr size_t cacheBytes = 1000000;
    constexpr u16 chunkCells = 32;

    struct RenderCacheBlock final: public IntrusiveNode, public Newable {
        u64 hash = 0;
        RenderCell cells[chunkCells];
    };

    constexpr size_t blockCount = cacheBytes / sizeof(RenderCacheBlock);

    constexpr size_t hashBucketCount() {
        size_t result = 1;
        while (result < blockCount * 2) {
            result <<= 1;
        }
        return result;
    }

    constexpr size_t bucketCount = hashBucketCount();
    constexpr size_t bucketMask = bucketCount - 1;

    static_assert(blockCount != 0);
    static_assert(stdHasTrivialDestructor(RenderCacheBlock));

    struct RenderCacheImpl final: public RenderCache {
        RenderCacheImpl();

        void beginFrame(u64 context) override;
        RenderCacheResult render(const TerminalCell* cells, u16 count, u8 lineAttribute, u32 index, RenderCell* scratch, RenderCellSpan* spans, const RenderCacheCallback& callback) override;
        size_t spanCapacity(u16 columns, u16 rows) const noexcept override;

        Buffer storage_;
        Vector<RenderCacheBlock*> freeBlocks_;
        Vector<RenderCacheBlock*> retiredBlocks_;
        IntrusiveList lru_;
        RenderCacheBlock* buckets_[bucketCount]{};
        u64 context_ = 0;

        void clear();
        void retire(RenderCacheBlock* block);
        const RenderCell* renderChunk(const TerminalCell* cells, u16 count, u8 lineAttribute, RenderCell* scratch, const RenderCacheCallback& callback);
    };
}

RenderCacheImpl::RenderCacheImpl()
    : storage_(cacheBytes)
{
    storage_.setCapacity(cacheBytes);
    freeBlocks_.grow(blockCount);
    retiredBlocks_.grow(blockCount);
    u8* const storage = (u8*)(storage_.mutData());
    for (size_t index = 0; index < blockCount; ++index) {
        freeBlocks_.pushBack(new (storage + index * sizeof(RenderCacheBlock)) RenderCacheBlock);
    }
}

void RenderCacheImpl::clear() {
    while (!lru_.empty()) {
        auto* const block = static_cast<RenderCacheBlock*>(lru_.popBack());
        buckets_[block->hash & bucketMask] = nullptr;
        freeBlocks_.pushBack(block);
    }
}

void RenderCacheImpl::retire(RenderCacheBlock* block) {
    buckets_[block->hash & bucketMask] = nullptr;
    block->remove();
    retiredBlocks_.pushBack(block);
}

void RenderCacheImpl::beginFrame(u64 context) {
    while (!retiredBlocks_.empty()) {
        freeBlocks_.pushBack(retiredBlocks_.popBack());
    }
    if (context_ != context) {
        clear();
        context_ = context;
    }
}

const RenderCell* RenderCacheImpl::renderChunk(const TerminalCell* cells, u16 count, u8 lineAttribute, RenderCell* scratch, const RenderCacheCallback& callback) {
    u64 hash = shash64(cells, (size_t)(count) * sizeof(TerminalCell));
    hash ^= ((u64)(lineAttribute) + 1) * 0x9e3779b97f4a7c15ULL;
    RenderCacheBlock*& bucket = buckets_[hash & bucketMask];
    if (bucket != nullptr && bucket->hash == hash) {
        bucket->remove();
        lru_.pushFront(bucket);
        return bucket->cells;
    }
    if (bucket != nullptr) {
        retire(bucket);
    }
    if (freeBlocks_.empty()) {
        if (!lru_.empty()) {
            retire(static_cast<RenderCacheBlock*>(lru_.mutBack()));
        }
        callback.render(cells, scratch, count, lineAttribute);
        return scratch;
    }

    RenderCacheBlock* const block = freeBlocks_.popBack();
    block->hash = hash;
    bucket = block;
    lru_.pushFront(block);
    callback.render(cells, block->cells, count, lineAttribute);
    return block->cells;
}

RenderCacheResult RenderCacheImpl::render(const TerminalCell* cells, u16 count, u8 lineAttribute, u32 index, RenderCell* scratch, RenderCellSpan* spans, const RenderCacheCallback& callback) {
    for (u16 offset = 0; offset < count;) {
        const u16 chunk = count - offset < chunkCells ? count - offset : chunkCells;
        const RenderCell* const output = renderChunk(cells + offset, chunk, lineAttribute, scratch, callback);
        spans->index = index + offset;
        spans->count = chunk;
        spans->cells = output;
        ++spans;
        if (output == scratch) {
            scratch += chunk;
        }
        offset += chunk;
    }
    return {scratch, spans};
}

size_t RenderCacheImpl::spanCapacity(u16 columns, u16 rows) const noexcept {
    return (size_t)(rows) * ((columns + chunkCells - 1) / chunkCells);
}

RenderCache* RenderCache::create(Composer& composer) {
    return composer.pool->make<RenderCacheImpl>();
}
