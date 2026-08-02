/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "vterm_headless.h"

#include "composer.h"
#include "pty.h"
#include "vterm.h"

#include <plt/window.h>

#include <std/ios/output.h>
#include <std/lib/buffer.h>
#include <std/mem/obj_pool.h>
#include <std/tst/ut.h>

using namespace stl;

namespace {
    struct CaptureOutput final: public Output {
        size_t writeImpl(const void* data, size_t size) override;

        Buffer bytes;
    };
}

size_t CaptureOutput::writeImpl(const void* data, size_t size) {
    bytes.append(data, size);
    return size;
}

STD_TEST_SUITE(VtermHeadless) {
    STD_TEST(InstallsMissingComposerDependencies) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());

        VtermHeadless::create(composer, nullptr);

        STD_INSIST(composer.platform != nullptr);
        STD_INSIST(composer.window != nullptr);
        STD_INSIST(composer.window->primary() != nullptr);
        STD_INSIST(composer.window->secondary() != nullptr);
        STD_INSIST(composer.ptyOutput != nullptr);
        STD_INSIST(composer.ptyMutex != nullptr);
        STD_INSIST(composer.pty != nullptr);
        STD_INSIST(composer.pty->output() == composer.ptyOutput);
        STD_INSIST(composer.vterm != nullptr);
    }

    STD_TEST(KeepsFallbackTitleForTerminalReset) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        VtermHeadless::create(composer, nullptr);
        const u8 reset[] = {'\x1b', 'c'};

        composer.vterm->feedPty(StringView(reset, sizeof(reset)));

        STD_INSIST(composer.vterm->output() != nullptr);
    }

    STD_TEST(PtyAndTerminalOutputsAreConsumedIndependently) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        CaptureOutput pty;
        composer.ptyOutput = &pty;
        VtermHeadless::create(composer, nullptr);
        Vterm& terminal = *composer.vterm;
        if (terminal.output() != nullptr) {
            terminal.consume();
        }
        const u8 input[] = {'a', 0x1b, '[', 'c'};

        terminal.feedPty(StringView(input, sizeof(input)));

        STD_INSIST(!pty.bytes.empty());
        STD_INSIST(terminal.output() != nullptr);
        pty.bytes.reset();
        STD_INSIST(pty.bytes.empty());
        STD_INSIST(terminal.output() != nullptr);
        terminal.consume();
        STD_INSIST(terminal.output() == nullptr);
    }

    STD_TEST(FeedConsumesTerminalAndPtyOutput) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        CaptureOutput pty;
        composer.ptyOutput = &pty;
        VtermHeadless* const headless = VtermHeadless::create(composer, nullptr);
        const u8 input[] = {'a', 0x1b, '[', 'c'};

        headless->feed(input, sizeof(input));

        STD_INSIST(!pty.bytes.empty());
        STD_INSIST(composer.vterm->output() == nullptr);

        pty.bytes.reset();
        headless->feed(input, sizeof(input));

        STD_INSIST(!pty.bytes.empty());
        STD_INSIST(composer.vterm->output() == nullptr);
    }

    STD_TEST(RawDeviceAttributesDoesNotProducePtyOutputInUtf8Mode) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        CaptureOutput pty;
        composer.ptyOutput = &pty;
        VtermHeadless::create(composer, nullptr);
        const u8 rawDeviceAttributes = 0x9a;

        composer.vterm->feedPty(StringView(&rawDeviceAttributes, 1));

        STD_INSIST(pty.bytes.empty());
        composer.vterm->feedPty(StringView(u8"\x1bZ"));
        STD_INSIST(!pty.bytes.empty());
    }

    STD_TEST(RawDeviceAttributesWorksInSingleByteMode) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        CaptureOutput pty;
        composer.ptyOutput = &pty;
        VtermHeadless::create(composer, nullptr);
        const u8 input[] = {'\x1b', '%', '@', 0x9a};

        composer.vterm->feedPty(StringView(input, sizeof(input)));

        STD_INSIST(!pty.bytes.empty());
    }
}
