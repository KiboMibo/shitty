#include "composer.h"
#include "vterm_headless.h"

#include <std/mem/obj_pool.h>

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t len) {
    static stl::ObjPool::Ref pool = stl::ObjPool::fromMemory();
    static Composer composer{pool.mutPtr()};
    static VtermHeadless* vterm = VtermHeadless::create(composer);
    vterm->feed((const u8*)(data), len);
    return 0;
}
