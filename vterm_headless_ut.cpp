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

    static void discardOutput(Vterm& terminal) {
        if (terminal.output() != nullptr) {
            terminal.consume();
        }
    }

    static void insistMatchingCursor(Vterm& whole, Vterm& split) {
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

    static void feedInFuzzChunks(Vterm& terminal, const u8* bytes, size_t size) {
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
        STD_INSIST(composer.pty != nullptr);
        STD_INSIST(composer.pty->output() == composer.ptyOutput);
        STD_INSIST(composer.vterm != nullptr);
    }

    // A tab is a second terminal behind the same window. Two of them must
    // be able to exist at once on one Composer: the terminal actions are
    // claimed once for the window, not once per terminal, and each
    // terminal contributes its own node to the action's listeners.
    STD_TEST(SecondVtermCoexistsOnOneComposer) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        VtermHeadless::create(composer, nullptr);
        Vterm* const first = composer.vterm;

        Vterm* const second = Vterm::create(composer, nullptr);

        STD_INSIST(first != nullptr);
        STD_INSIST(second != nullptr);
        STD_INSIST(first != second);
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

    STD_TEST(KeepsUtf8GraphemeInputIndependentOfPtyChunking) {
        // Saved fuzz state: the final record splits a ZWJ sequence after a
        // wide glyph wraps into, then is discarded by, a double-width row.
        const u8 corpus[] = {
            0x68, 0x65, 0x1b, 0x5b, 0x64, 0x1b, 0x08, 0x0b, 0x1b, 0x23, 0x33, 0x31,
            0x34, 0x31, 0x3b, 0x2b, 0x58, 0x5b, 0x35, 0x38, 0x3b, 0x35,
            0x3b, 0x31, 0x31,
            0xc6, 0xc4, 0xce, 0xc7, 0xc4, 0xcc, 0xc7, 0xc4, 0x32, 0x3b, 0x31, 0x34,
            0x00, 0x06, 0x1b, 0x5b, 0x3f, 0x36, 0x39, 0x68, 0x68, 0x0e, 0x1b, 0x5b,
            0x33, 0x34, 0x3b, 0x33, 0x36, 0x73, 0x00, 0x35, 0x1b, 0x5b, 0x3f, 0x36,
            0x39, 0x68, 0x1b, 0x5b, 0x35, 0x3b, 0x33, 0x30, 0x72, 0x1b, 0x5b, 0x35,
            0x31, 0x3b, 0x36, 0x32, 0x73, 0x00, 0x04, 0x1b, 0x5b, 0x35, 0x6e, 0xb1,
            0x02, 0x8d, 0x23, 0x00, 0x55, 0x1b, 0x5b, 0x31, 0x35, 0x3b, 0x31, 0x39,
            0x48, 0x1b, 0x5b, 0x33, 0x32, 0x44,

            0x33, 0x48, 0x1b, 0x5b, 0x35, 0x31, 0x47, 0x1b, 0x5b, 0x3f, 0x36, 0x68,
            0x1b, 0x5b, 0x5b, 0x31, 0x23, 0x0f, 0x9f, 0x91, 0x80, 0x8d, 0xd0, 0x6c,
            0x68, 0x1b, 0x5b, 0x34, 0x68, 0x65, 0x1b, 0x5b, 0x64, 0x1b, 0x08, 0x0b,
            0x1b, 0x23, 0x33, 0x31, 0x34, 0x31, 0x3b, 0x2b, 0x35, 0x6c, 0x1b, 0x5b,
            0x32,
            0x30, 0x68, 0x1b, 0x23, 0x34, 0x80, 0xfe, 0x09, 0x1b, 0x5b, 0x37, 0x3b,
            0x31, 0x38, 0x33, 0x48, 0x31, 0x47, 0x1b, 0x5b, 0x3f, 0x36, 0x68, 0xf0,
            0x5b,

            0x32, 0x30, 0x68, 0xd7, 0x90, 0x0d, 0x0a, 0x00, 0x3e, 0x1b, 0x5b, 0x3f,
            0x32, 0x4b, 0x1b, 0x5b, 0x31, 0x36, 0x49, 0xf0, 0x9f, 0x91, 0xa9, 0xe2,
            0x80, 0x8d, 0xf0, 0x9f, 0x95, 0xa9, 0xe2, 0x80, 0x8d, 0xf0, 0x9f, 0x91,
            0x1b, 0x5b, 0x5b, 0x3f, 0x31, 0x51, 0x51, 0x51, 0x51, 0x51, 0x51, 0x51,
            0x30, 0x68,
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

    STD_TEST(BulkUtf8DecoderMatchesByteWiseDecoder) {
        // The whole-buffer feed decodes through placeUtf8Run, tiny feeds
        // through Utf8Decoder::pushByte.  Screens must match cell for cell
        // for every replacement-character rule and chunk-boundary split.
        const u8 directed[] =
            // Valid 2-, 3- and 4-byte sequences with edge codepoints.
            u8"A\xc3\xa9 \xe2\x82\xac \xf0\x9f\x92\xbb "
            u8"\xe0\xa0\x80 \xed\x9f\xbf \xf4\x8f\xbf\xbf Z\r\n"
            // Stray continuations: the C1 range resets grapheme input.
            u8"\x80\x9f\xa0\xbf Z\r\n"
            // Bytes that can never begin a sequence.
            u8"\xc0\xc1\xf5\xff Z\r\n"
            // Overlong, surrogate and beyond-U+10FFFF first continuations.
            u8"\xe0\x80 \xe0\x9f \xed\xa0 \xf0\x80 \xf4\x90 Z\r\n"
            // Leads truncated at every position.
            u8"\xc2Z \xe2Z \xe2\x82Z \xf0Z \xf0\x90Z \xf0\x90\x8fZ\r\n"
            // Combining, wide and joined clusters against garbage.
            u8"e\xcc\x81 \xe4\xbd\xa0 \xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb \x80\xcc\x81 Z\r\n"
            // Controls inside a pending sequence are transparent to the
            // streaming decoder: the sequence completes around them.
            u8"\xe2\x07\x82\xac \xc3\x07\xa9 \xe2\x82\x07\xac \xf0\x9f\x00\x92\xbb \xe2\x7f\x82\xac Z\r\n";

        // Deterministic garbage over the full byte range except ESC: mode
        // and charset changes are covered by directed tests elsewhere.
        u8 garbage[4096];
        u32 state = 0x2545f491;
        for (size_t index = 0; index < sizeof(garbage); ++index) {
            state = state * 747796405u + 2891336453u;
            const u8 byte = (u8)(state >> 24);
            garbage[index] = byte == 0x1b ? 0x20 : byte;
        }

        const auto compareScreens = [](Vterm& whole, Vterm& split, u16 columns) {
            whole.expose();
            split.expose();
            const TerminalUpdate* const wholeUpdate = whole.output();
            const TerminalUpdate* const splitUpdate = split.output();
            STD_INSIST(wholeUpdate != nullptr);
            STD_INSIST(splitUpdate != nullptr);
            STD_INSIST(wholeUpdate->cursor.posX == splitUpdate->cursor.posX);
            STD_INSIST(wholeUpdate->cursor.posY == splitUpdate->cursor.posY);
            STD_INSIST(wholeUpdate->rowCount == splitUpdate->rowCount);
            STD_INSIST(wholeUpdate->rowCount > 0);
            for (size_t index = 0; index < wholeUpdate->rowCount; ++index) {
                const TerminalRow& wholeRow = wholeUpdate->rows[index];
                const TerminalRow& splitRow = splitUpdate->rows[index];
                STD_INSIST(wholeRow.row == splitRow.row);
                STD_INSIST(wholeRow.lineAttribute == splitRow.lineAttribute);
                for (u16 cell = 0; cell < columns; ++cell) {
                    STD_INSIST(wholeRow.cells[cell].style == splitRow.cells[cell].style);
                    STD_INSIST(wholeRow.cells[cell].content == splitRow.cells[cell].content);
                }
            }
            whole.consume();
            split.consume();
        };

        const size_t chunkSizes[] = {1, 2, 3, 7};
        for (const size_t chunk : chunkSizes) {
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

            whole.feedPty(StringView(directed, sizeof(directed) - 1));
            for (size_t offset = 0; offset < sizeof(directed) - 1; offset += chunk) {
                const size_t length = sizeof(directed) - 1 - offset < chunk ? sizeof(directed) - 1 - offset : chunk;
                split.feedPty(StringView(directed + offset, length));
            }
            compareScreens(whole, split, wholeComposer.columns);

            whole.feedPty(StringView(garbage, sizeof(garbage)));
            for (size_t offset = 0; offset < sizeof(garbage); offset += chunk) {
                const size_t length = sizeof(garbage) - offset < chunk ? sizeof(garbage) - offset : chunk;
                split.feedPty(StringView(garbage + offset, length));
            }
            compareScreens(whole, split, wholeComposer.columns);
        }
    }
}
