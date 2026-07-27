/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "base64.h"

#include <std/lib/buffer.h>
#include <std/str/view.h>
#include <std/tst/ut.h>

#include <cstring>

using namespace stl;

namespace {
    bool bytesEqual(const Buffer& buffer, StringView expected) {
        return StringView(buffer) == expected;
    }
}

STD_TEST_SUITE(Base64) {
    STD_TEST(EncodesKnownVectors) {
        Buffer output;

        STD_INSIST(bytesEqual(base64Encode(StringView(u8""), output), StringView(u8"")));
        STD_INSIST(bytesEqual(base64Encode(StringView(u8"f"), output), StringView(u8"Zg==")));
        STD_INSIST(bytesEqual(base64Encode(StringView(u8"fo"), output), StringView(u8"Zm8=")));
        STD_INSIST(bytesEqual(base64Encode(StringView(u8"foo"), output), StringView(u8"Zm9v")));
        STD_INSIST(bytesEqual(base64Encode(StringView(u8"foob"), output), StringView(u8"Zm9vYg==")));
        STD_INSIST(bytesEqual(base64Encode(StringView(u8"fooba"), output), StringView(u8"Zm9vYmE=")));
        STD_INSIST(bytesEqual(base64Encode(StringView(u8"foobar"), output), StringView(u8"Zm9vYmFy")));
    }

    STD_TEST(RoundTripsBinaryData) {
        const u8 bytes[] = {0, 1, 2, 0x7f, 0x80, 0xfe, 0xff};
        Buffer encoded;
        Buffer decoded;
        bool valid = false;

        base64Encode(StringView(bytes, sizeof(bytes)), encoded);
        base64Decode(StringView(encoded), decoded, valid);

        STD_INSIST(valid);
        STD_INSIST(decoded.used() == sizeof(bytes));
        STD_INSIST(StringView(decoded) == StringView(bytes, sizeof(bytes)));
    }

    STD_TEST(RoundTripsEveryByteAcrossBlockBoundaries) {
        u8 bytes[257];
        for (size_t index = 0; index < sizeof(bytes); ++index) {
            bytes[index] = (u8)index;
        }

        Buffer encoded;
        Buffer decoded;
        for (size_t length = 0; length <= sizeof(bytes); ++length) {
            const StringView input(bytes, length);
            bool valid = false;
            base64Encode(input, encoded);
            base64Decode(StringView(encoded), decoded, valid);
            STD_INSIST(valid);
            STD_INSIST(StringView(decoded) == input);
        }
    }

    STD_TEST(AcceptsCanonicalUnpaddedTail) {
        Buffer output;
        bool valid = false;

        base64Decode(StringView(u8"Zg"), output, valid);
        STD_INSIST(valid);
        STD_INSIST(bytesEqual(output, StringView(u8"f")));

        base64Decode(StringView(u8"Zm8"), output, valid);
        STD_INSIST(valid);
        STD_INSIST(bytesEqual(output, StringView(u8"fo")));
    }

    STD_TEST(RejectsWhitespaceAndMalformedInput) {
        Buffer output;
        output.append("stale", 5);
        bool valid = true;

        base64Decode(StringView(u8"Zm 9v"), output, valid);
        STD_INSIST(!valid);
        STD_INSIST(output.empty());

        base64Decode(StringView(u8"Z"), output, valid);
        STD_INSIST(!valid);
        STD_INSIST(output.empty());

        base64Decode(StringView(u8"Zm9*"), output, valid);
        STD_INSIST(!valid);
        STD_INSIST(output.empty());
    }

    STD_TEST(RejectsBadPaddingAndNonCanonicalTails) {
        const StringView malformed[] = {
            StringView(u8"="),
            StringView(u8"===="),
            StringView(u8"Zg="),
            StringView(u8"Zg==="),
            StringView(u8"Zm=8"),
            StringView(u8"Zm8=="),
            StringView(u8"Zg==Zg=="),
            StringView(u8"Zh=="),
            StringView(u8"Zm9="),
            StringView(u8"Zh"),
            StringView(u8"Zm9"),
        };
        Buffer output;

        for (const StringView input : malformed) {
            output.reset();
            output.append("stale", 5);
            bool valid = true;
            base64Decode(input, output, valid);
            STD_INSIST(!valid);
            STD_INSIST(output.empty());
        }
    }

    STD_TEST(ReturnsCallerBuffer) {
        Buffer output;
        bool valid = false;

        STD_INSIST(&base64Encode(StringView(u8"x"), output) == &output);
        STD_INSIST(&base64Decode(StringView(u8"eA=="), output, valid) == &output);
        STD_INSIST(valid);
    }

    STD_TEST(InPlaceDecoderMatchesOneShotValidation) {
        const StringView inputs[] = {
            StringView(u8""),
            StringView(u8"Zg"),
            StringView(u8"Zg=="),
            StringView(u8"Zm8"),
            StringView(u8"Zm8="),
            StringView(u8"Zm9v"),
            StringView(u8"="),
            StringView(u8"Z"),
            StringView(u8"Zg="),
            StringView(u8"Zg==="),
            StringView(u8"Zm=8"),
            StringView(u8"Zg==Zg=="),
            StringView(u8"Zh=="),
            StringView(u8"Zm9="),
            StringView(u8"Zh"),
            StringView(u8"Zm9"),
        };

        for (const StringView input : inputs) {
            Buffer expected;
            bool expectedValid = false;
            base64Decode(input, expected, expectedValid);

            u8 actual[16];
            memcpy(actual, input.data(), input.length());
            size_t actualSize = input.length();
            const bool actualValid = base64DecodeInPlace(actual, actualSize);
            STD_INSIST(actualValid == expectedValid);
            if (actualValid) {
                STD_INSIST(StringView(actual, actualSize) == StringView(expected));
            }
        }
    }

    STD_TEST(DecodesInPlace) {
        u8 bytes[] = u8"AAECA3+A/v8=";
        const u8 expected[] = {0, 1, 2, 3, 0x7f, 0x80, 0xfe, 0xff};
        size_t size = sizeof(bytes) - 1;

        STD_INSIST(base64DecodeInPlace(bytes, size));
        STD_INSIST(StringView(bytes, size) == StringView(expected, sizeof(expected)));
    }
}
