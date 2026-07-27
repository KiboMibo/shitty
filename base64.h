/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/sys/types.h>

namespace stl {
    class Buffer;
    class StringView;
}

struct Base64Decoder {
    void reset() noexcept;
    bool push(u8 byte, stl::Buffer& output);
    bool finish(stl::Buffer& output);

    u8 values[4] = {};
    u8 count = 0;
    bool padding = false;
    bool complete = false;
    bool valid = true;
};

stl::Buffer& base64Encode(stl::StringView input, stl::Buffer& output);
stl::Buffer& base64Decode(stl::StringView input, stl::Buffer& output, bool& valid);
