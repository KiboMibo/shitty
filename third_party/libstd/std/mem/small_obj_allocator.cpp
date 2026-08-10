#include "small_obj_allocator.h"

#include "asan.h"
#include "obj_pool.h"
#include "free_list.h"

#include <std/dbg/assert.h>

using namespace stl;

namespace {
    constexpr size_t minimumSize = 16;
    constexpr size_t classCount = 8;

    static_assert(minimumSize << (classCount - 1) == smallObjMaxSize);

    size_t classFor(size_t size) noexcept {
        return 64 - (size_t)__builtin_clzll((size - 1) | (minimumSize - 1)) - 4;
    }

    class SmallObjAllocatorImpl final: public SmallObjAllocator {
    public:
        explicit SmallObjAllocatorImpl(ObjPool* pool);

        void* allocate(size_t size) override;
        void deallocate(void* pointer, size_t size) noexcept override;

    private:
        ObjPool* pool_;
        FreeList* classes_[classCount] = {};
    };
}

SmallObjAllocatorImpl::SmallObjAllocatorImpl(ObjPool* pool)
    : pool_(pool)
{
}

void* SmallObjAllocatorImpl::allocate(size_t size) {
    STD_ASSERT(size != 0);
    STD_ASSERT(size <= smallObjMaxSize);

    const size_t index = classFor(size);
    FreeList*& freeList = classes_[index];
    if (freeList == nullptr) {
        freeList = FreeList::create(pool_, minimumSize << index);
    }
    void* const result = freeList->allocate();
    const size_t allocationSize = minimumSize << index;
    // The class slot can be larger than the object. Keep that slack
    // poisoned so ASan sees it as the allocation's right redzone.
    asanPoisonMemory((u8*)(result) + size, allocationSize - size);
    return result;
}

void SmallObjAllocatorImpl::deallocate(void* pointer, size_t size) noexcept {
    STD_ASSERT(pointer != nullptr);
    STD_ASSERT(size != 0);
    STD_ASSERT(size <= smallObjMaxSize);

    classes_[classFor(size)]->release(pointer);
}

SmallObjAllocator::~SmallObjAllocator() noexcept {
}

SmallObjAllocator* SmallObjAllocator::create(ObjPool* pool) {
    return pool->make<SmallObjAllocatorImpl>(pool);
}
