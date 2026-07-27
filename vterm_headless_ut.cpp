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
    STD_TEST(PtyAndTerminalOutputHaveIndependentLifetime) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        VtermHeadless::create(composer);
        Vterm* const vterm = composer.vterm;
        STD_INSIST(vterm->output() != nullptr);
        vterm->consume();
        const u8 input[] = {'a', 0x1b, '[', 'c'};

        vterm->feedPty(StringView(input, sizeof(input)));

        const StringView ptyOutput = vterm->ptyOutput();
        STD_INSIST(!ptyOutput.empty());
        vterm->consumePtyOutput(ptyOutput.length());
        STD_INSIST(vterm->ptyOutput().empty());
        STD_INSIST(vterm->output() != nullptr);
        vterm->consume();
        STD_INSIST(vterm->output() == nullptr);

        vterm->feedPty(StringView(input, sizeof(input)));

        STD_INSIST(vterm->output() != nullptr);
        vterm->consume();
        STD_INSIST(!vterm->ptyOutput().empty());
    }

    STD_TEST(FeedConsumesTerminalAndPtyOutput) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        VtermHeadless* const headless = VtermHeadless::create(composer);
        const u8 input[] = {'a', 0x1b, '[', 'c'};

        headless->feed(input, sizeof(input));

        STD_INSIST(composer.vterm->ptyOutput().empty());
        STD_INSIST(composer.vterm->output() == nullptr);

        headless->feed(input, sizeof(input));

        STD_INSIST(composer.vterm->ptyOutput().empty());
        STD_INSIST(composer.vterm->output() == nullptr);
    }
}
