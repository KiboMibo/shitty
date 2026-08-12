#pragma once

#include <std/sys/types.h>

#if defined(__has_feature)
    #if __has_feature(address_sanitizer)
        #define STD_MEM_ASAN_ENABLED 1
    #endif
#endif

#if defined(__SANITIZE_ADDRESS__) && !defined(STD_MEM_ASAN_ENABLED)
    #define STD_MEM_ASAN_ENABLED 1
#endif

#ifndef STD_MEM_ASAN_ENABLED
    #define STD_MEM_ASAN_ENABLED 0
#endif

namespace stl {
    inline constexpr bool addressSanitizerEnabled = STD_MEM_ASAN_ENABLED != 0;

#if STD_MEM_ASAN_ENABLED
    extern "C" {
        void __asan_poison_memory_region(const volatile void* address, size_t size);
        void __asan_unpoison_memory_region(const volatile void* address, size_t size);
        int __asan_address_is_poisoned(const volatile void* address);
    }

    inline void asanPoisonMemory(void* address, size_t size) noexcept {
        if (size != 0) {
            __asan_poison_memory_region(address, size);
        }
    }

    inline void asanUnpoisonMemory(void* address, size_t size) noexcept {
        if (size != 0) {
            __asan_unpoison_memory_region(address, size);
        }
    }

    inline bool asanMemoryIsPoisoned(const void* address) noexcept {
        return __asan_address_is_poisoned(address) != 0;
    }
#else
    inline void asanPoisonMemory(void*, size_t) noexcept {
    }

    inline void asanUnpoisonMemory(void*, size_t) noexcept {
    }

    inline bool asanMemoryIsPoisoned(const void*) noexcept {
        return false;
    }
#endif
}

#undef STD_MEM_ASAN_ENABLED
