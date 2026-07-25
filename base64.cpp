/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "base64.h"

#include <std/lib/buffer.h>
#include <std/str/view.h>

#if __has_include(<simdutf.h>)
    #include <simdutf.h>
    #define SHITTY_BASE64_SIMDUTF 1
#else
    #define SHITTY_BASE64_SIMDUTF 0
#endif

using namespace stl;

namespace {
#if SHITTY_BASE64_SIMDUTF
    bool containsSpace(StringView input) noexcept {
        for (const u8 byte : input) {
            if (byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r' || byte == '\f') {
                return true;
            }
        }
        return false;
    }
#else
    constexpr u8 alphabet[] = u8"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    u8 decodeValue(u8 byte) noexcept {
        if (byte >= u8'A' && byte <= u8'Z') {
            return byte - u8'A';
        }
        if (byte >= u8'a' && byte <= u8'z') {
            return byte - u8'a' + 26;
        }
        if (byte >= u8'0' && byte <= u8'9') {
            return byte - u8'0' + 52;
        }
        if (byte == u8'+') {
            return 62;
        }
        if (byte == u8'/') {
            return 63;
        }
        return (u8)0xff;
    }
#endif
}

Buffer& base64Encode(StringView input, Buffer& output) {
#if SHITTY_BASE64_SIMDUTF
    output.reset();
    output.grow(simdutf::base64_length_from_binary(input.length()));
    const size_t size = simdutf::binary_to_base64((const char*)(input.data()), input.length(), (char*)(output.mutData()));
    output.seekAbsolute(size);
    return output;
#else
    output.reset();
    const size_t outputSize = input.length() / 3 * 4 + (input.length() % 3 == 0 ? 0 : 4);
    output.grow(outputSize);

    auto* encoded = (u8*)output.mutData();
    size_t source = 0;
    size_t target = 0;
    while (input.length() - source >= 3) {
        const u32 bits = ((u32)input[source] << 16) | ((u32)input[source + 1] << 8) | (u32)input[source + 2];
        encoded[target++] = alphabet[(bits >> 18) & 0x3f];
        encoded[target++] = alphabet[(bits >> 12) & 0x3f];
        encoded[target++] = alphabet[(bits >> 6) & 0x3f];
        encoded[target++] = alphabet[bits & 0x3f];
        source += 3;
    }

    const size_t remainder = input.length() - source;
    if (remainder != 0) {
        u32 bits = (u32)input[source] << 16;
        if (remainder == 2) {
            bits |= (u32)input[source + 1] << 8;
        }
        encoded[target++] = alphabet[(bits >> 18) & 0x3f];
        encoded[target++] = alphabet[(bits >> 12) & 0x3f];
        encoded[target++] = remainder == 2 ? alphabet[(bits >> 6) & 0x3f] : u8'=';
        encoded[target++] = u8'=';
    }

    output.seekAbsolute(target);
    return output;
#endif
}

Buffer& base64Decode(StringView input, Buffer& output, bool& valid) {
#if SHITTY_BASE64_SIMDUTF
    output.reset();
    valid = false;
    if (containsSpace(input)) {
        return output;
    }

    output.grow(simdutf::maximal_binary_length_from_base64((const char*)(input.data()), input.length()));
    const size_t remainder = input.length() % 4;
    const simdutf::last_chunk_handling_options handling = remainder == 2 || remainder == 3 ? simdutf::loose : simdutf::strict;
    const simdutf::result result = simdutf::base64_to_binary((const char*)(input.data()), input.length(), (char*)(output.mutData()), simdutf::base64_default, handling);
    if (result.error != simdutf::SUCCESS) {
        return output;
    }

    if (handling == simdutf::loose) {
        char tail[4];
        for (size_t index = 0; index < remainder; ++index) {
            tail[index] = (char)(input[input.length() - remainder + index]);
        }
        for (size_t index = remainder; index < sizeof(tail); ++index) {
            tail[index] = '=';
        }
        char decodedTail[3];
        const simdutf::result tailResult = simdutf::base64_to_binary(tail, sizeof(tail), decodedTail, simdutf::base64_default, simdutf::strict);
        if (tailResult.error != simdutf::SUCCESS) {
            return output;
        }
    }

    output.seekAbsolute(result.count);
    valid = true;
    return output;
#else
    output.reset();
    valid = false;

    size_t padding = 0;
    if (!input.empty() && input.back() == u8'=') {
        ++padding;
        if (input.length() > 1 && input[input.length() - 2] == u8'=') {
            ++padding;
        }
    }
    if (padding != 0 && input.length() % 4 != 0) {
        return output;
    }

    const size_t encodedLength = input.length() - padding;
    const size_t remainder = encodedLength % 4;
    if (remainder == 1 || (padding == 1 && remainder != 3) || (padding == 2 && remainder != 2)) {
        return output;
    }

    const size_t outputSize = encodedLength / 4 * 3 + (remainder == 0 ? 0 : remainder - 1);
    output.grow(outputSize);
    auto* decoded = (u8*)output.mutData();

    size_t source = 0;
    size_t target = 0;
    while (encodedLength - source >= 4) {
        const u8 a = decodeValue(input[source]);
        const u8 b = decodeValue(input[source + 1]);
        const u8 c = decodeValue(input[source + 2]);
        const u8 d = decodeValue(input[source + 3]);
        if (a == (u8)0xff || b == (u8)0xff || c == (u8)0xff || d == (u8)0xff) {
            return output;
        }
        decoded[target++] = (u8)((a << 2) | (b >> 4));
        decoded[target++] = (u8)((b << 4) | (c >> 2));
        decoded[target++] = (u8)((c << 6) | d);
        source += 4;
    }

    if (remainder != 0) {
        const u8 a = decodeValue(input[source]);
        const u8 b = decodeValue(input[source + 1]);
        if (a == (u8)0xff || b == (u8)0xff || ((b & 0x0f) != 0 && remainder == 2)) {
            return output;
        }
        decoded[target++] = (u8)((a << 2) | (b >> 4));

        if (remainder == 3) {
            const u8 c = decodeValue(input[source + 2]);
            if (c == (u8)0xff || (c & 0x03) != 0) {
                return output;
            }
            decoded[target++] = (u8)((b << 4) | (c >> 2));
        }
    }

    output.seekAbsolute(target);
    valid = true;
    return output;
#endif
}
