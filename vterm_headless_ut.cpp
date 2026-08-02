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

    void discardOutput(Vterm& terminal) {
        if (terminal.output() != nullptr) {
            terminal.consume();
        }
    }

    void insistMatchingCursor(Vterm& whole, Vterm& split) {
        whole.expose();
        split.expose();
        const TerminalUpdate* const wholeUpdate = whole.output();
        const TerminalUpdate* const splitUpdate = split.output();
        STD_INSIST(wholeUpdate != nullptr);
        STD_INSIST(splitUpdate != nullptr);
        STD_INSIST(wholeUpdate->cursor.posX == splitUpdate->cursor.posX);
        STD_INSIST(wholeUpdate->cursor.posY == splitUpdate->cursor.posY);
        whole.consume();
        split.consume();
    }

    void feedInFuzzChunks(Vterm& terminal, const u8* bytes, size_t size) {
        const size_t first = bytes[0] % size;
        const size_t second = first + bytes[1] % (size - first);
        terminal.feedPty(StringView(bytes, first));
        terminal.feedPty(StringView(bytes + first, second - first));
        terminal.feedPty(StringView(bytes + second, size - second));
    }
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

    STD_TEST(KeepsDoubleWidthOutputIndependentOfPtyChunking) {
        // Record format is the fuzz target's [op, size, pty bytes] stream.
        // All three records are pty input; the split form mirrors main_fuzz.
        const u8 corpus[] = {
            0x00, 0x41, 0x1b, 0x23, 0x36, 0xd7, 0x31, 0x67, 0x1b, 0x5b, 0x31, 0x30,
            0x30, 0x49, 0x1b, 0x5b, 0x34, 0x37, 0x5a, 0x1b, 0x5b, 0x35, 0x38, 0x3b,
            0x35, 0x3b, 0x32, 0x33, 0x33, 0x3b, 0x32, 0x35, 0x3b, 0x36, 0x38, 0x3b,
            0x34, 0x3a, 0x35, 0x3b, 0x34, 0x38, 0x3b, 0xa4, 0x35, 0x3b, 0x34, 0x38,
            0x6d, 0x1b, 0x5b, 0x31, 0x32, 0x3b, 0x33, 0x36, 0x48, 0x1b, 0x5b, 0x33,
            0x37, 0x42, 0x00, 0x3d, 0x1b, 0x5b, 0x34, 0x3b, 0x32, 0x24, 0x70, 0x1b,
            0x48, 0x1b, 0x5b, 0x31, 0x67, 0x1b, 0x5b, 0x32, 0x37, 0x49, 0x1b, 0x5b,
            0x39, 0x30, 0x5a, 0x1b, 0x5b, 0x3f, 0x32, 0x4a, 0x1b, 0x5b, 0x33, 0x31,
            0x4c, 0x1b, 0x5b, 0x3f, 0x36, 0x39, 0x68, 0x1b, 0x5b, 0x31, 0x38, 0x3b,
            0x32, 0x38, 0x72, 0x1b, 0x5b, 0x33, 0x33, 0x3b, 0x36, 0x38, 0x73, 0x1b,
            0x3b, 0x5b, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61,
            0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0xe1, 0x61,
            0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61,
            0x0a, 0x1b, 0x5d, 0x31, 0x31, 0x30, 0x3b, 0x72, 0x51, 0x51, 0x51, 0x51,
            0x51, 0x51, 0x51, 0x51, 0x51, 0x30, 0x4a, 0x1b, 0x5b, 0x33, 0x31, 0x54,
        };
        auto wholePool = ObjPool::fromMemory();
        auto splitPool = ObjPool::fromMemory();
        Composer& wholeComposer = *wholePool->make<Composer>(wholePool.mutPtr());
        Composer& splitComposer = *splitPool->make<Composer>(splitPool.mutPtr());
        VtermHeadless::create(wholeComposer, nullptr);
        VtermHeadless::create(splitComposer, nullptr);
        Vterm& whole = *wholeComposer.vterm;
        Vterm& split = *splitComposer.vterm;
        discardOutput(whole);
        discardOutput(split);

        size_t offset = 0;
        while (offset + 2 <= sizeof(corpus)) {
            const u8 op = corpus[offset++];
            const size_t size = corpus[offset++];
            STD_INSIST(op < 192);
            STD_INSIST(offset + size <= sizeof(corpus));
            whole.feedPty(StringView(corpus + offset, size));
            feedInFuzzChunks(split, corpus + offset, size);
            insistMatchingCursor(whole, split);
            offset += size;
        }
        STD_INSIST(offset == sizeof(corpus));
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
