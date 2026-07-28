/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "vterm_headless.h"

#include "composer.h"
#include "vterm.h"

#include <std/mem/obj_pool.h>
#include <std/tst/ut.h>

using namespace stl;

STD_TEST_SUITE(VtermHeadless) {
    STD_TEST(FeedConsumesTerminalAndPtyOutput) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        VtermHeadless* const headless = VtermHeadless::create(composer);
        const u8 input[] = {'a', 0x1b, '[', 'c'};

        headless->feed(input, sizeof(input));

        VtermOutput output = composer.vterm->output();
        STD_INSIST(output.pty.empty());
        STD_INSIST(output.terminal == nullptr);

        headless->feed(input, sizeof(input));

        output = composer.vterm->output();
        STD_INSIST(output.pty.empty());
        STD_INSIST(output.terminal == nullptr);
    }
}
