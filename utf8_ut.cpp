/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "utf8.h"

#include <std/lib/buffer.h>
#include <std/str/view.h>
#include <std/tst/ut.h>

using namespace stl;

namespace {
    bool bytesEqual(const Buffer& buffer, StringView expected) {
        return StringView(buffer) == expected;
    }

    Buffer& encodeCodepoint(u32 codepoint, Buffer& output) {
        output.reset();
        Utf8Encoder::pushUnicode(codepoint, [&](u8 byte) {
            output.append(&byte, 1);
        });
        return output;
    }
}

STD_TEST_SUITE(Utf8) {
    STD_TEST(EncoderCoversEveryLength) {
        Buffer output;

        STD_INSIST(bytesEqual(encodeCodepoint(0x24, output), StringView(u8"\x24")));
        STD_INSIST(bytesEqual(encodeCodepoint(0xa2, output), StringView(u8"\xc2\xa2")));
        STD_INSIST(bytesEqual(encodeCodepoint(0x20ac, output), StringView(u8"\xe2\x82\xac")));
        STD_INSIST(bytesEqual(encodeCodepoint(0x1f642, output), StringView(u8"\xf0\x9f\x99\x82")));
    }

    STD_TEST(DecoderCompletesMultibyteSequences) {
        Utf8Decoder decoder;

        STD_INSIST(decoder.pushByte(0xe2) == 0);
        STD_INSIST(decoder.expectsContinuation());
        STD_INSIST(decoder.pushByte(0x82) == 0);
        STD_INSIST(decoder.pushByte(0xac) == 1);
        STD_INSIST(!decoder.expectsContinuation());
        STD_INSIST(decoder.getUnicode() == 0x20ac);
    }

    STD_TEST(DecoderRejectsOverlongSurrogateAndOutOfRangeSequences) {
        Utf8Decoder decoder;

        STD_INSIST(decoder.pushByte(0xe0) == 0);
        STD_INSIST(decoder.pushByte(0x80) == 0);
        STD_INSIST(decoder.pushByte(0x80) == 1);
        STD_INSIST(decoder.getUnicode() == Unicode_Replacement_Character);

        STD_INSIST(decoder.pushByte(0xed) == 0);
        STD_INSIST(decoder.pushByte(0xa0) == 0);
        STD_INSIST(decoder.pushByte(0x80) == 1);
        STD_INSIST(decoder.getUnicode() == Unicode_Replacement_Character);

        STD_INSIST(decoder.pushByte(0xf4) == 0);
        STD_INSIST(decoder.pushByte(0x90) == 0);
        STD_INSIST(decoder.pushByte(0x80) == 0);
        STD_INSIST(decoder.pushByte(0x80) == 1);
        STD_INSIST(decoder.getUnicode() == Unicode_Replacement_Character);
    }

    STD_TEST(DecoderReportsStrayAndInterruptedContinuation) {
        Utf8Decoder decoder;

        STD_INSIST(decoder.pushByte(0x80) == 1);
        STD_INSIST(decoder.getUnicode() == Unicode_Replacement_Character);

        STD_INSIST(decoder.pushByte(0xe2) == 0);
        STD_INSIST(decoder.pushByte(0x41) == 2);
        STD_INSIST(decoder.getUnicode() == Unicode_Replacement_Character);
        STD_INSIST(!decoder.expectsContinuation());
    }

    STD_TEST(DecoderFlushesPrematureEndOnce) {
        Utf8Decoder decoder;
        decoder.pushByte(0xf0);
        decoder.pushByte(0x9f);

        STD_INSIST(decoder.checkPrematureEOS());
        STD_INSIST(decoder.getUnicode() == Unicode_Replacement_Character);
        STD_INSIST(!decoder.checkPrematureEOS());
    }

    STD_TEST(DecoderResetAndDirectUnicode) {
        Utf8Decoder decoder;
        decoder.pushByte(0xe2);
        decoder.reset();

        STD_INSIST(!decoder.expectsContinuation());
        STD_INSIST(decoder.getUnicode() == 0);
        STD_INSIST(!decoder.onUnicode(0));
        STD_INSIST(decoder.onUnicode(0x1f642));
        STD_INSIST(decoder.getUnicode() == 0x1f642);
    }
}
