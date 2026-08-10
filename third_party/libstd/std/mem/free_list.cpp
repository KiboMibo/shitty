#include "free_list.h"
#include "asan.h"
#include "obj_pool.h"

#include <std/alg/minmax.h>

using namespace stl;

namespace {
    struct Node {
        Node* next;
    };

    struct Impl: public FreeList {
        ObjPool* pool;
        size_t objSize;
        Node* freeList;

        Impl(ObjPool* p, size_t os) noexcept
            : pool(p)
            , objSize(max(os, sizeof(Node)))
            , freeList(nullptr)
        {
        }

        void* allocate() override {
            if (freeList) {
                Node* const result = freeList;
                // A released slot is poisoned in full. Open it before
                // reading the in-band link, and leave it accessible for
                // the caller that now owns the slot.
                asanUnpoisonMemory(result, objSize);
                freeList = result->next;
                return result;
            }

            void* const result = pool->allocate(objSize);
            asanUnpoisonMemory(result, objSize);
            return result;
        }

        void release(void* ptr) noexcept override {
            // SmallObjAllocator may have poisoned its size-class tail as a
            // redzone, so expose the whole slot while writing the link.
            asanUnpoisonMemory(ptr, objSize);
            auto node = (Node*)ptr;
            node->next = freeList;
            freeList = node;
            asanPoisonMemory(ptr, objSize);
        }
    };
}

FreeList::~FreeList() noexcept {
}

FreeList* FreeList::create(ObjPool* pool, size_t objSize) {
    return pool->make<Impl>(pool, objSize);
}
