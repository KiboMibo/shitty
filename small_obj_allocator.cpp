/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "small_obj_allocator.h"

#include <std/mem/free_list.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    constexpr size_t minimumSize = 16;
    constexpr size_t classCount = 8;

    static_assert(minimumSize << (classCount - 1) == smallObjMaxSize);

    size_t classFor(size_t size) {
        return 64 - (size_t)__builtin_clzll((size - 1) | (minimumSize - 1)) - 4;
    }

    struct SmallObjAllocatorImpl final: public SmallObjAllocator {
        explicit SmallObjAllocatorImpl(ObjPool* pool);

        void* allocate(size_t size) override;
        void deallocate(void* pointer, size_t size) override;

        ObjPool* pool;
        FreeList* classes[classCount] = {};
    };
}

SmallObjAllocatorImpl::SmallObjAllocatorImpl(ObjPool* pool_)
    : pool(pool_)
{
}

void* SmallObjAllocatorImpl::allocate(size_t size) {
    const size_t index = classFor(size);
    FreeList*& freeList = classes[index];
    if (freeList == nullptr) {
        freeList = FreeList::create(pool, minimumSize << index);
    }
    return freeList->allocate();
}

void SmallObjAllocatorImpl::deallocate(void* pointer, size_t size) {
    classes[classFor(size)]->release(pointer);
}

SmallObjAllocator* SmallObjAllocator::create(ObjPool* pool) {
    return pool->make<SmallObjAllocatorImpl>(pool);
}
