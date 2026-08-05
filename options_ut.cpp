/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "options.h"

#include <std/mem/obj_pool.h>
#include <std/tst/ut.h>

#include <cstring>

using namespace stl;

STD_TEST_SUITE(Options) {
    STD_TEST(CreatedInstancesOwnTheirParsedLists) {
        auto pool = ObjPool::fromMemory();
        char program[] = "st";
        char config[] = "-config";
        char emptyConfig[] = "/dev/null";
        char font[] = "-font";
        char firstName[] = "first-font";
        char secondName[] = "second-font";
        char* firstArgv[] = {program, config, emptyConfig, font, firstName, nullptr};
        char* secondArgv[] = {program, config, emptyConfig, font, secondName, nullptr};

        Options* const first = Options::create(*pool, firstArgv, 5);
        Options* const second = Options::create(*pool, secondArgv, 5);

        STD_INSIST(first->fontnames.length() == 1);
        STD_INSIST(second->fontnames.length() == 1);
        STD_INSIST(first->fontnames[0] == StringView(u8"first-font"));
        STD_INSIST(second->fontnames[0] == StringView(u8"second-font"));
        STD_INSIST(firstArgv[1] == nullptr);
        STD_INSIST(secondArgv[1] == nullptr);
    }
}
