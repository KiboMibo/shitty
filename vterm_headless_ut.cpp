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
    STD_TEST(PtyAndTerminalOutputsAreConsumedIndependently) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        VtermHeadless::create(composer);
        Vterm& terminal = *composer.vterm;
        if (terminal.output() != nullptr) {
            terminal.consume();
        }
        const u8 input[] = {'a', 0x1b, '[', 'c'};

        terminal.feedPty(StringView(input, sizeof(input)));

        const StringView pty = terminal.ptyOutput();
        STD_INSIST(!pty.empty());
        STD_INSIST(terminal.output() != nullptr);
        terminal.consumePtyOutput(pty.length());
        STD_INSIST(terminal.ptyOutput().empty());
        STD_INSIST(terminal.output() != nullptr);
        terminal.consume();
        STD_INSIST(terminal.output() == nullptr);
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

    STD_TEST(RawDeviceAttributesDoesNotProducePtyOutputInUtf8Mode) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        VtermHeadless::create(composer);
        const u8 rawDeviceAttributes = 0x9a;

        composer.vterm->feedPty(StringView(&rawDeviceAttributes, 1));

        STD_INSIST(composer.vterm->ptyOutput().empty());
        composer.vterm->feedPty(StringView(u8"\x1bZ"));
        STD_INSIST(!composer.vterm->ptyOutput().empty());
    }

    STD_TEST(RawDeviceAttributesWorksInSingleByteMode) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        VtermHeadless::create(composer);
        const u8 input[] = {'\x1b', '%', '@', 0x9a};

        composer.vterm->feedPty(StringView(input, sizeof(input)));

        STD_INSIST(!composer.vterm->ptyOutput().empty());
    }
}
