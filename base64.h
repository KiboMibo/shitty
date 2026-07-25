/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

namespace stl {
    class Buffer;
    class StringView;
}

stl::Buffer& base64Encode(stl::StringView input, stl::Buffer& output);
stl::Buffer& base64Decode(stl::StringView input, stl::Buffer& output, bool& valid);
