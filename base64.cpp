/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "base64.h"

#include <std/lib/buffer.h>
#include <std/str/view.h>

#include <simdutf.h>

using namespace stl;

namespace {
    bool containsSpace(StringView input) noexcept {
        for (const u8 byte : input) {
            if (byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r' || byte == '\f') {
                return true;
            }
        }
        return false;
    }
}

Buffer& base64Encode(StringView input, Buffer& output) {
    output.reset();
    output.grow(simdutf::base64_length_from_binary(input.length()));
    const size_t size = simdutf::binary_to_base64((const char*)(input.data()), input.length(), (char*)(output.mutData()));
    output.seekAbsolute(size);
    return output;
}

Buffer& base64Decode(StringView input, Buffer& output, bool& valid) {
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
}
