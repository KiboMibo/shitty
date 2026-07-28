/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/alg/destruct.h>
#include <std/mem/embed.h>
#include <std/mem/new.h>
#include <std/sys/types.h>

namespace stl {
    class ObjPool;
}

inline constexpr size_t smallObjMaxSize = 2048;

struct SmallObjAllocator {
    virtual void* allocate(size_t size) = 0;
    virtual void deallocate(void* pointer, size_t size) = 0;

    template <typename T, typename... Args>
    T* make(Args&&... args) {
        struct Storage: public stl::Embed<T>, public stl::Newable {
            using stl::Embed<T>::Embed;
        };

        static_assert(sizeof(Storage) == sizeof(T));
        static_assert(sizeof(T) <= smallObjMaxSize);
        static_assert(alignof(T) <= alignof(max_align_t));
        return &(new (allocate(sizeof(T))) Storage(stl::forward<Args>(args)...))->t;
    }

    template <typename T>
    void release(T* object) {
        deallocate(stl::destruct(object), sizeof(T));
    }

    static SmallObjAllocator* create(stl::ObjPool* pool);
};
