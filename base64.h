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

stl::Buffer& base64Encode(stl::StringView input, stl::Buffer& output);
bool base64DecodeInPlace(u8* data, size_t& size) noexcept;
