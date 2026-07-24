/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "composer.h"
#include "vterm_headless.h"

#include <std/mem/obj_pool.h>

#include <cstddef>
#include <cstdint>

namespace stl {}

using namespace stl;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t len) {
    static ObjPool::Ref pool = ObjPool::fromMemory();
    static Composer& composer = *pool->make<Composer>(pool.mutPtr());
    static VtermHeadless* vterm = VtermHeadless::create(composer);
    vterm->feed((const u8*)(data), len);
    return 0;
}
