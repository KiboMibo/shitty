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

#if !SHITTY_BASE64_SIMDUTF
    constexpr u8 alphabet[] = u8"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
#endif
}

bool base64DecodeInPlace(u8* data, size_t& size) noexcept {
    size_t source = 0;
    size_t target = 0;
    while (size - source >= 4) {
        const u8 first = decodeValue(data[source]);
        const u8 second = decodeValue(data[source + 1]);
        if ((first | second) == (u8)0xff) {
            return false;
        }

        if (data[source + 2] == '=') {
            if (data[source + 3] != '=' || source + 4 != size || (second & 0x0f) != 0) {
                return false;
            }
            data[target++] = (first << 2) | (second >> 4);
            size = target;
            return true;
        }

        const u8 third = decodeValue(data[source + 2]);
        if (third == (u8)0xff) {
            return false;
        }
        if (data[source + 3] == '=') {
            if (source + 4 != size || (third & 0x03) != 0) {
                return false;
            }
            data[target++] = (first << 2) | (second >> 4);
            data[target++] = (second << 4) | (third >> 2);
            size = target;
            return true;
        }

        const u8 fourth = decodeValue(data[source + 3]);
        if (fourth == (u8)0xff) {
            return false;
        }
        data[target++] = (first << 2) | (second >> 4);
        data[target++] = (second << 4) | (third >> 2);
        data[target++] = (third << 6) | fourth;
        source += 4;
    }

    const size_t remainder = size - source;
    if (remainder == 1) {
        return false;
    }
    if (remainder >= 2) {
        const u8 first = decodeValue(data[source]);
        const u8 second = decodeValue(data[source + 1]);
        if ((first | second) == (u8)0xff || (remainder == 2 && (second & 0x0f) != 0)) {
            return false;
        }
        data[target++] = (first << 2) | (second >> 4);
        if (remainder == 3) {
            const u8 third = decodeValue(data[source + 2]);
            if (third == (u8)0xff || (third & 0x03) != 0) {
                return false;
            }
            data[target++] = (second << 4) | (third >> 2);
        }
    }
    size = target;
    return true;
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
